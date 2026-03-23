#include "mainwindow.h"
#include "addwindowdialog.h"
#include "aggregatechatform.h"
#include "../utils/applystyle.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QFileDialog>
#include <QFileInfo>
#include <QCheckBox>
#include <QProcess>
#include <QDir>
#include <QToolButton>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWindow>
#include <functional>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

// ==================== Batch add overlay ====================

class BatchAddOverlayWidget : public QWidget
{
public:
    explicit BatchAddOverlayWidget(QWidget* parent = nullptr)
        : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setAutoFillBackground(false);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(100, 100, 110, 55));
    }
};

// ==================== WeChat RPA calibration overlay ====================

class WechatCalibrationOverlay : public QWidget
{
public:
    explicit WechatCalibrationOverlay(QWidget* parent = nullptr)
        : QWidget(parent,
                  Qt::Tool
                      | Qt::FramelessWindowHint
                      | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setOnFinished(std::function<void(bool, const QRect&)> cb) { m_onFinished = std::move(cb); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor(20, 20, 24, 120));

        // help text
        p.setPen(Qt::white);
        p.setFont(QFont(p.font().family(), 10));
        const QString tip = QStringLiteral("框选微信“聊天气泡滚动区”（Esc取消，回车确认）");
        p.drawText(QRect(0, 8, width(), 24), Qt::AlignHCenter | Qt::AlignVCenter, tip);

        if (m_selecting || m_selection.isValid()) {
            QRect r = m_selection.normalized();
            p.setPen(QPen(QColor(70, 170, 255, 220), 2));
            p.setBrush(QColor(70, 170, 255, 40));
            p.drawRoundedRect(r, 6, 6);

            p.setPen(Qt::white);
            p.drawText(r.adjusted(8, 8, -8, -8),
                       Qt::AlignLeft | Qt::AlignTop,
                       QStringLiteral("%1x%2").arg(r.width()).arg(r.height()));
        }
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) return;
        m_selecting = true;
        m_origin = e->pos();
        m_selection = QRect(m_origin, m_origin);
        update();
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!m_selecting) return;
        m_selection = QRect(m_origin, e->pos());
        update();
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) return;
        m_selecting = false;
        m_selection = QRect(m_origin, e->pos());
        update();
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape) {
            finish(false);
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            finish(true);
            return;
        }
        QWidget::keyPressEvent(e);
    }

private:
    void finish(bool ok)
    {
        if (m_onFinished) {
            m_onFinished(ok, m_selection.normalized());
        }
        deleteLater();
    }

    bool m_selecting = false;
    QPoint m_origin;
    QRect m_selection;
    std::function<void(bool, const QRect&)> m_onFinished;
};

// ==================== Tree roles & delegate ====================

namespace {
enum PlatformTreeRole {
    PlatformIdRole = Qt::UserRole,
    IsGroupRole,
    DotColorRole,
    IsCustomerServiceItemRole,
    IsActivatedRole
};

QIcon resourceIcon(const QString& path, const QIcon& fallback = {})
{
    const QIcon icon(path);
    return icon.isNull() ? fallback : icon;
}

QPixmap resourcePixmap(const QString& path, const QSize& size, const QIcon& fallback = {})
{
    const QIcon icon = resourceIcon(path, fallback);
    return icon.pixmap(size);
}

QIcon customerServiceIcon(const QString& platformId)
{
    if (platformId == QLatin1String("qianniu"))
        return resourceIcon(QStringLiteral(":/qianniu_logo.svg"));
    if (platformId == QLatin1String("pinduoduo"))
        return resourceIcon(QStringLiteral(":/pinduoduo_logo.svg"));
    if (platformId == QLatin1String("douyin"))
        return resourceIcon(QStringLiteral(":/doudian_logo.svg"));
    return {};
}

const QStringList& builtinEncouragementMessages()
{
    static const QStringList messages = {
        QStringLiteral("山高万仞，只登一步"),
        QStringLiteral("悦己者自成山海"),
        QStringLiteral("珍惜当下"),
        QStringLiteral("今天也在变好"),
        QStringLiteral("先完成，再完美"),
        QStringLiteral("保持热爱，奔赴山海"),
        QStringLiteral("向前走，自有答案"),
        QStringLiteral("认真生活，自会发光")
    };

    return messages;
}

QStringList normalizedMessages(const QStringList& messages)
{
    QStringList normalized;
    for (QString message : messages) {
        message = message.trimmed();
        if (message.isEmpty() || normalized.contains(message))
            continue;
        normalized.append(message);
    }
    return normalized;
}

QString formatRect(const QRect& r)
{
    return QStringLiteral("x=%1 y=%2 w=%3 h=%4")
        .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

QStringList loadCustomEncouragementMessages()
{
    QSettings settings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    return normalizedMessages(settings.value(QStringLiteral("statusBar/customMessages")).toStringList());
}

void saveCustomEncouragementMessages(const QStringList& messages)
{
    QSettings settings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    settings.setValue(QStringLiteral("statusBar/customMessages"), normalizedMessages(messages));
}

QStringList allEncouragementMessages(const QStringList& customMessages)
{
    QStringList messages = builtinEncouragementMessages();
    const QStringList custom = normalizedMessages(customMessages);
    for (const QString& message : custom) {
        if (!messages.contains(message))
            messages.append(message);
    }
    return messages;
}

QString randomEncouragementText(const QStringList& messages, const QString& currentText = {})
{
    if (messages.isEmpty())
        return {};
    if (messages.size() == 1)
        return messages.first();

    QString nextText;
    do {
        const int index = QRandomGenerator::global()->bounded(messages.size());
        nextText = messages.at(index);
    } while (nextText == currentText);

    return nextText;
}

bool isWechatWindow(const WindowInfo& info)
{
    return MainWindow::isWechatWindowInfo(info);
}

bool isInWindowCloseHotspot(const QRect& rect, const QPoint& cursorPos)
{
    if (!rect.isValid()) {
        return false;
    }

    const int hotspotWidth = qMin(96, qMax(56, rect.width() / 6));
    const int hotspotHeight = qMin(48, qMax(28, rect.height() / 12));
    const QRect closeHotspot(rect.right() - hotspotWidth + 1,
                             rect.top(),
                             hotspotWidth,
                             hotspotHeight);
    return closeHotspot.contains(cursorPos);
}

QIcon onlinePlatformFallbackIcon(const WindowInfo& info)
{
    const QString proc = info.processName.toLower();
    const QString title = info.platformName.toLower();

    if (proc.contains(QStringLiteral("wechat")) || title.contains(QStringLiteral("wechat"))
        || info.platformName.contains(QStringLiteral("微信"))) {
        return resourceIcon(QStringLiteral(":/wechat_logo.svg"));
    }
    if (proc.contains(QStringLiteral("msedge")) || title.contains(QStringLiteral("edge"))) {
        return resourceIcon(QStringLiteral(":/edge_logo.svg"));
    }
    if (proc.contains(QStringLiteral("chrome")) || title.contains(QStringLiteral("chrome"))) {
        return resourceIcon(QStringLiteral(":/chrome_logo.svg"));
    }
    return {};
}

class PlatformTreeDelegate : public QStyledItemDelegate
{
public:
    explicit PlatformTreeDelegate(QTreeView* tree, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_tree(tree) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const bool isGroup = index.data(IsGroupRole).toBool();
        return {option.rect.width(), isGroup ? 44 : 50};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const ApplyStyle::MainWindowTheme theme = ApplyStyle::loadSavedMainWindowTheme();
        const PlatformTreeColors c = ApplyStyle::platformTreeColors(theme);

        const QString title = index.data(Qt::DisplayRole).toString();
        const bool isGroup = index.data(IsGroupRole).toBool();
        const bool expanded = m_tree && m_tree->isExpanded(index);
        const bool sel = (option.state & QStyle::State_Selected) != 0;
        const bool hover = (option.state & QStyle::State_MouseOver) != 0;

        QRect r = option.rect.adjusted(6, 3, -6, -3);

        if (isGroup) {
            const QColor dotColor = index.data(DotColorRole).value<QColor>();
            QColor bg = c.groupBgDefault;
            if (hover) bg = c.groupBgHover;
            if (sel) bg = c.groupBgSelected;
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
            painter->setPen(c.groupTextColor);
            const bool hasChildren = index.model() && index.model()->rowCount(index) > 0;
            const int textRight = hasChildren ? r.right() - 28 : r.right() - 8;
            QRect textRect(r.left() + 12 + dot + 8, r.top(), textRight - (r.left() + 12 + dot + 8), r.height());
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);

            if (hasChildren) {
                const QIcon expandIcon(QStringLiteral(":/fold_arrow_to_expand_icon.svg"));
                const QIcon collapseIcon(QStringLiteral(":/fold_arrow_to_collapse_icon.svg"));
                const QSize arrowSize(16, 16);
                QRect arrowRect(r.right() - 24, r.top(), 20, r.height());
                QPixmap pix = (expanded ? collapseIcon : expandIcon).pixmap(arrowSize);
                if (!pix.isNull()) {
                    QPoint pt(arrowRect.center().x() - pix.width() / 2, arrowRect.center().y() - pix.height() / 2);
                    painter->drawPixmap(pt, pix);
                }
            }
        } else {
            bool isCS = index.data(IsCustomerServiceItemRole).toBool();
            bool isActivated = index.data(IsActivatedRole).toBool();

            QColor bg;
            if (isCS && !isActivated) {
                bg = c.itemInactiveBgDefault;
                if (hover) bg = c.itemInactiveBgHover;
                if (sel) bg = c.itemInactiveBgSelected;
            } else {
                bg = c.itemBgDefault;
                if (hover) bg = c.itemBgHover;
                if (sel) bg = c.itemBgSelected;
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(r, 10, 10);

            int xOff = r.left() + 12;

            if (isCS) {
                int dotSz = 8;
                QColor dotClr = isActivated ? c.csDotActivated : c.csDotInactive;
                QRect dotR(xOff, r.center().y() - dotSz / 2, dotSz, dotSz);
                painter->setBrush(dotClr);
                painter->drawEllipse(dotR);
                xOff += dotSz + 8;
            }

            QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            QSize iconSize(22, 22);
            QRect iconRect(xOff, r.center().y() - iconSize.height() / 2, iconSize.width(), iconSize.height());
            if (!icon.isNull()) {
                if (isCS && !isActivated) {
                    auto pix = icon.pixmap(iconSize);
                    painter->setOpacity(c.itemInactiveIconOpacity);
                    painter->drawPixmap(iconRect, pix);
                    painter->setOpacity(1.0);
                } else {
                    icon.paint(painter, iconRect, Qt::AlignCenter);
                }
            }
            xOff += iconSize.width() + 10;

            QColor textClr = (isCS && !isActivated) ? c.itemInactiveTextColor : c.itemTextColor;
            painter->setPen(textClr);
            QRect textRect(xOff, r.top(), r.right() - 8 - xOff, r.height());
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);

            if (sel && !isCS) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(c.itemAccentBarColor);
                painter->drawRoundedRect(QRect(r.left(), r.top() + 6, 3, r.height() - 12), 1, 1);
            }
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
    button->setIconSize(QSize(18, 18));
    return button;
}
}

// ==================== MainWindow ====================

MainWindow::MainWindow(const QString& username, QWidget* parent)
    : QMainWindow(parent)
    , m_username(username)
{
    setWindowTitle(QString("AI客服 - %1").arg(username));
    setWindowIcon(resourceIcon(QStringLiteral(":/app_icon.svg"),
                               qApp->style()->standardIcon(QStyle::SP_DesktopIcon)));
    setMinimumSize(1100, 680);
    resize(1440, 840);

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
    buildStatusBar();
    m_windowStateTimer = new QTimer(this);
    m_windowStateTimer->setInterval(250);
    connect(m_windowStateTimer, &QTimer::timeout,
            this, &MainWindow::checkManagedWindowsState);
    m_windowStateTimer->start();
    showSystemReadyPage();
}

MainWindow::~MainWindow()
{
}

// ==================== Left Sidebar ====================

QWidget* MainWindow::buildLeftSidebar()
{
    auto* left = new QWidget(this);
    left->setObjectName("leftSidebar");
    left->setMinimumWidth(200);

    auto* layout = new QVBoxLayout(left);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(6);
    layout->setAlignment(Qt::AlignTop);

    m_platformTreeModel = new QStandardItemModel(this);

    // -- 在线平台 --
    m_onlineGroup = new QStandardItem(QStringLiteral("在线平台"));
    m_onlineGroup->setData(QStringLiteral("online"), PlatformIdRole);
    m_onlineGroup->setData(true, IsGroupRole);
    m_onlineGroup->setData(QColor(0, 200, 120), DotColorRole);
    m_onlineGroup->setFlags(m_onlineGroup->flags() & ~Qt::ItemIsDropEnabled);

    // -- 管理后台（可折叠分组） --
    m_manageGroup = new QStandardItem(QStringLiteral("管理后台"));
    m_manageGroup->setData(QStringLiteral("manage"), PlatformIdRole);
    m_manageGroup->setData(true, IsGroupRole);
    m_manageGroup->setData(QColor(24, 144, 255), DotColorRole);
    m_manageGroup->setFlags(m_manageGroup->flags() & ~Qt::ItemIsDropEnabled);

    const QIcon iconRobot = resourceIcon(QStringLiteral(":/platform_management_icon.svg"),
                                         qApp->style()->standardIcon(QStyle::SP_ComputerIcon));
    auto* itemRobot = new QStandardItem(iconRobot, QStringLiteral("机器人管理"));
    itemRobot->setData(QStringLiteral("robot"), PlatformIdRole);
    itemRobot->setData(false, IsGroupRole);
    itemRobot->setData(false, IsCustomerServiceItemRole);
    m_manageGroup->appendRow(itemRobot);

    const QIcon iconReception = resourceIcon(QStringLiteral(":/aggregate_reception_icons/message_icon.svg"),
                                             qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation));
    auto* itemReception = new QStandardItem(iconReception, QStringLiteral("聚合接待"));
    itemReception->setData(QStringLiteral("aggregate"), PlatformIdRole);
    itemReception->setData(false, IsGroupRole);
    itemReception->setData(false, IsCustomerServiceItemRole);
    m_manageGroup->appendRow(itemReception);

    // -- 客服平台 --
    m_csGroup = new QStandardItem(QStringLiteral("客服平台"));
    m_csGroup->setData(QStringLiteral("cs"), PlatformIdRole);
    m_csGroup->setData(true, IsGroupRole);
    m_csGroup->setData(QColor(160, 160, 160), DotColorRole);
    m_csGroup->setFlags(m_csGroup->flags() & ~Qt::ItemIsDropEnabled);

    struct CsItem { const char* name; const char* id; };
    CsItem csItems[] = {{"千牛", "qianniu"}, {"拼多多", "pinduoduo"}, {"抖店", "douyin"}};
    for (const auto& cs : csItems) {
        const QString platformId = QString::fromUtf8(cs.id);
        QIcon itemIcon = customerServiceIcon(platformId);
        if (itemIcon.isNull())
            itemIcon = qApp->style()->standardIcon(QStyle::SP_DialogApplyButton);
        auto* item = new QStandardItem(itemIcon, QString::fromUtf8(cs.name));
        item->setData(platformId, PlatformIdRole);
        item->setData(false, IsGroupRole);
        item->setData(true, IsCustomerServiceItemRole);
        item->setData(false, IsActivatedRole);
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        m_csGroup->appendRow(item);
    }

    m_platformTreeModel->appendRow(m_onlineGroup);
    m_platformTreeModel->appendRow(m_manageGroup);
    m_platformTreeModel->appendRow(m_csGroup);

    m_platformTree = new QTreeView(left);
    m_platformTree->setObjectName("platformList");
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
    m_platformTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_platformTree->expandAll();
    layout->addWidget(m_platformTree);

    updateTreeViewHeight();
    connect(m_platformTree->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onPlatformTreeSelectionChanged);
    connect(m_platformTree, &QTreeView::clicked,
            this, &MainWindow::onPlatformTreeClicked);
    connect(m_platformTree, &QTreeView::expanded,
            this, [this](const QModelIndex&) { updateTreeViewHeight(); });
    connect(m_platformTree, &QTreeView::collapsed,
            this, [this](const QModelIndex&) { updateTreeViewHeight(); });
    connect(m_platformTree, &QTreeView::customContextMenuRequested,
            this, &MainWindow::showPlatformContextMenu);

    return left;
}

// ==================== Top Bar ====================

QWidget* MainWindow::buildTopBar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName("topBar");
    bar->setFixedHeight(52);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(10);

    auto* logo = new QLabel(bar);
    logo->setObjectName("logo");
    logo->setFixedSize(22, 22);
    logo->setPixmap(resourcePixmap(QStringLiteral(":/app_icon.svg"), QSize(22, 22),
                                   qApp->style()->standardIcon(QStyle::SP_DesktopIcon)));

    auto* title = new QLabel(QStringLiteral("AI客服 - %1").arg(m_username), bar);
    title->setObjectName("topTitle");

    layout->addWidget(logo);
    layout->addWidget(title);
    layout->addSpacing(8);

    m_btnAdd = makeTopIconButton(bar, resourceIcon(QStringLiteral(":/add_new_window_icon.svg"),
                                                   qApp->style()->standardIcon(QStyle::SP_FileDialogNewFolder)), QStringLiteral("添加新窗口"));
    m_btnRefresh = makeTopIconButton(bar, resourceIcon(QStringLiteral(":/refresh_platform_list_icon.svg"),
                                                       qApp->style()->standardIcon(QStyle::SP_BrowserReload)), QStringLiteral("刷新平台列表"));
    auto* bugBtn = makeTopIconButton(bar, QIcon(QStringLiteral(":/bug_log_icon.svg")),
                                     QStringLiteral("查看 Bug 修复日志"));
    auto* helpBtn = makeTopIconButton(bar, QIcon(QStringLiteral(":/question_mark_icon.svg")),
                                      QStringLiteral("查看软件使用说明"));
    layout->addWidget(m_btnAdd);
    layout->addWidget(m_btnRefresh);

    auto* readyWrap = new QWidget(bar);
    readyWrap->setObjectName("readyWrap");
    auto* readyLayout = new QHBoxLayout(readyWrap);
    readyLayout->setContentsMargins(10, 6, 10, 6);
    readyLayout->setSpacing(6);
    auto* readyIcon = new QLabel(readyWrap);
    readyIcon->setPixmap(resourcePixmap(QStringLiteral(":/system_ready_icon.svg"), QSize(18, 18),
                                        qApp->style()->standardIcon(QStyle::SP_ArrowUp)));
    auto* readyText = new QLabel(QStringLiteral("系统就绪"), readyWrap);
    readyText->setObjectName("readyText");
    readyLayout->addWidget(readyIcon);
    readyLayout->addWidget(readyText);
    layout->addWidget(readyWrap);
    layout->addStretch(1);
    layout->addWidget(bugBtn);
    layout->addWidget(helpBtn);

    connect(m_btnAdd, &QToolButton::clicked, this, &MainWindow::openAddWindowDialog);
    connect(m_btnRefresh, &QToolButton::clicked, this, &MainWindow::showSystemReadyPage);
    connect(bugBtn, &QToolButton::clicked, this, &MainWindow::openBugLogDialog);
    connect(helpBtn, &QToolButton::clicked, this, &MainWindow::openAppHelpDialog);
    return bar;
}

// ==================== Center Content ====================

QWidget* MainWindow::buildCenterContent()
{
    m_centerStack = new QStackedWidget(this);
    m_centerStack->setObjectName("centerStack");
    m_centerStack->addWidget(buildReadyPage()); // index 0

    m_placeholderPage = new QWidget(this);
    m_placeholderPage->setObjectName("placeholderPage");
    auto* phLayout = new QVBoxLayout(m_placeholderPage);
    phLayout->setContentsMargins(0, 0, 0, 0);
    phLayout->setAlignment(Qt::AlignCenter);
    m_placeholderLabel = new QLabel(m_placeholderPage);
    m_placeholderLabel->setText("placeholderText");
    m_placeholderLabel->setAlignment(Qt::AlignCenter);
    phLayout->addWidget(m_placeholderLabel);
    m_centerStack->addWidget(m_placeholderPage); // index 1

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
        totalHeight += isGroup ? 44 : 50;
        if (m_platformTree->isExpanded(index)) {
            int childCount = m_platformTreeModel->rowCount(index);
            totalHeight += childCount * 50;
        }
    }
    m_platformTree->setMinimumHeight(totalHeight + 20);
}

// ==================== Tree Navigation ====================

void MainWindow::onPlatformTreeSelectionChanged()
{
    QModelIndex idx = m_platformTree->currentIndex();
    if (!idx.isValid()) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        showSystemReadyPage();
        return;
    }

    QString id = idx.data(PlatformIdRole).toString();
    bool isGroup = idx.data(IsGroupRole).toBool();

    if (isGroup) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        showSystemReadyPage();
        return;
    }
    if (id == QLatin1String("aggregate")) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        openAggregateChatForm();
        return;
    }
    if (id == QLatin1String("manage") || id == QLatin1String("robot")) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        showPlaceholderPage(idx.data(Qt::DisplayRole).toString());
        return;
    }

    // Customer service item — not activated yet
    bool isCS = idx.data(IsCustomerServiceItemRole).toBool();
    bool isActivated = idx.data(IsActivatedRole).toBool();
    if (isCS && !isActivated) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        QString name = idx.data(Qt::DisplayRole).toString();
        showPlaceholderPage(QStringLiteral("请通过顶部「添加新窗口」按钮关联 %1 窗口").arg(name));
        return;
    }

    // Managed window
    if (m_managedWindows.contains(id)) {
        switchToWindow(id);
        return;
    }

    hideCurrentFloatWindow();
    m_activeWindowId.clear();
    showPlaceholderPage(idx.data(Qt::DisplayRole).toString());
}

void MainWindow::onPlatformTreeClicked(const QModelIndex& idx)
{
    if (!idx.isValid()) return;
    bool isGroup = idx.data(IsGroupRole).toBool();
    if (isGroup) {
        m_platformTree->setExpanded(idx, !m_platformTree->isExpanded(idx));
    }
}

// ==================== Page Switching ====================

void MainWindow::showSystemReadyPage()
{
    m_centerStack->setCurrentIndex(0);
}

void MainWindow::showPlaceholderPage(const QString& title)
{
    m_placeholderLabel->setText(QStringLiteral("%1").arg(title));
    m_centerStack->setCurrentWidget(m_placeholderPage);
}

void MainWindow::setupStyles()
{
    m_mainWindowTheme = ApplyStyle::loadSavedMainWindowTheme();
    setStyleSheet(ApplyStyle::mainWindowStyle(m_mainWindowTheme));
}

void MainWindow::applyMainWindowTheme(ApplyStyle::MainWindowTheme theme)
{
    m_mainWindowTheme = theme;
    ApplyStyle::saveMainWindowTheme(theme);
    setStyleSheet(ApplyStyle::mainWindowStyle(theme));
    if (m_platformTree && m_platformTree->viewport())
        m_platformTree->viewport()->update();
    if (m_aggregateChatForm)
        m_aggregateChatForm->applyTheme(theme);
}

static constexpr int kOneClickMinOnline = 2;
static constexpr int kOneClickMaxOnline = 50;

QWidget* MainWindow::buildReadyPage()
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
    rocket->setPixmap(resourcePixmap(QStringLiteral(":/rocket_icon.svg"), QSize(60, 60),
                                     qApp->style()->standardIcon(QStyle::SP_ArrowUp)));
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
    cardLayout->addWidget(divider, 0, Qt::AlignHCenter);
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
    auto* btnPick = makeQuick(resourceIcon(QStringLiteral(":/click_to_select_platform_icon.svg"),
                                           qApp->style()->standardIcon(QStyle::SP_ArrowRight)),
                              QStringLiteral("一键聚合"));
    m_btnOneClickAggregate = btnPick;
    updateOneClickAggregateTooltip();
    btnPick->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btnPick, &QToolButton::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(m_btnOneClickAggregate);
        QAction* setLimit = menu.addAction(QStringLiteral("设置在线窗口上限..."));
        QAction* triggered = menu.exec(m_btnOneClickAggregate->mapToGlobal(pos));
        if (triggered == setLimit) {
            const int cur = oneClickMaxOnlineLimit();
            bool ok = false;
            int v = QInputDialog::getInt(this, QStringLiteral("在线窗口上限"),
                                         QStringLiteral("上限为 %1～%2，建议不超过 30 以保持流畅：")
                                             .arg(kOneClickMinOnline).arg(kOneClickMaxOnline),
                                         cur, kOneClickMinOnline, kOneClickMaxOnline, 1, &ok);
            if (ok)
                setOneClickMaxOnlineLimit(v);
        }
    });
    connect(btnPick, &QToolButton::clicked, this, &MainWindow::startOneClickAggregate);
    auto* btnEmbed = makeQuick(resourceIcon(QStringLiteral(":/auto_embed_window_icon.svg"),
                                            qApp->style()->standardIcon(QStyle::SP_FileDialogListView)),
                               QStringLiteral("添加新窗口"));
    auto* btnStart = makeQuick(resourceIcon(QStringLiteral(":/quick_launch_application_icon.svg"),
                                            qApp->style()->standardIcon(QStyle::SP_DialogOkButton)),
                               QStringLiteral("快速启动应用"));
    m_btnQuickStart = btnStart;
    connect(btnEmbed, &QToolButton::clicked, this, &MainWindow::openAddWindowDialog);
    connect(btnStart, &QToolButton::clicked, this, &MainWindow::runQuickLaunchApps);
    btnStart->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btnStart, &QToolButton::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(m_btnQuickStart);
        QAction* manage = menu.addAction(QStringLiteral("管理应用列表"));
        QAction* triggered = menu.exec(m_btnQuickStart->mapToGlobal(pos));
        if (triggered == manage) {
            openQuickLaunchManager();
        }
    });
    quickLayout->addWidget(btnPick);
    quickLayout->addWidget(btnEmbed);
    quickLayout->addWidget(btnStart);
    layout->addWidget(m_readyCard, 0, Qt::AlignHCenter);
    layout->addWidget(quickRow, 0, Qt::AlignHCenter);
    layout->addStretch(2);

    return center;
}

// ==================== Add Window Dialog ====================

void MainWindow::openAddWindowDialog()
{
    AddWindowDialog dlg(this);
    dlg.exec();
}

int MainWindow::oneClickMaxOnlineLimit() const
{
    QSettings s(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    int v = s.value(QStringLiteral("oneClickAggregate/maxOnline"), 10).toInt();
    return qBound(kOneClickMinOnline, v, kOneClickMaxOnline);
}

void MainWindow::setOneClickMaxOnlineLimit(int n)
{
    int v = qBound(kOneClickMinOnline, n, kOneClickMaxOnline);
    QSettings s(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    s.setValue(QStringLiteral("oneClickAggregate/maxOnline"), v);
    updateOneClickAggregateTooltip();
}

void MainWindow::updateOneClickAggregateTooltip()
{
    if (m_btnOneClickAggregate) {
        const int limit = oneClickMaxOnlineLimit();
        m_btnOneClickAggregate->setToolTip(
            QStringLiteral("自动聚合可识别窗口（客服平台/微信/浏览器及普通应用），在线窗口上限为 %1（右键可修改）").arg(limit));
    }
}

void MainWindow::startOneClickAggregate()
{
    const QVector<WindowInfo> list = Win32WindowHelper::enumTopLevelWindows();
    QVector<WindowInfo> queue;
    queue.reserve(list.size());

    int onlineCount = 0;
    const int maxOnline = oneClickMaxOnlineLimit();

    for (const auto& info : list) {
        if (!info.handle)
            continue;
        bool alreadyManaged = false;
        for (auto it = m_managedWindows.constBegin(); it != m_managedWindows.constEnd(); ++it) {
            if (it.value().handle == info.handle) {
                alreadyManaged = true;
                break;
            }
        }
        if (alreadyManaged)
            continue;

        const QString csId = matchCustomerServicePlatform(info);
        if (!csId.isEmpty()) {
            queue.append(info);
            continue;
        }

        if (onlineCount >= maxOnline)
            continue;

        if (MainWindow::isWechatWindowInfo(info) || info.isBrowserLike) {
            queue.append(info);
            ++onlineCount;
        } else {
            queue.append(info);
            ++onlineCount;
        }
    }

    if (queue.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("未发现可聚合窗口"), 4000);
        return;
    }

    startBatchAddWindows(queue);
}

static void loadQuickLaunchConfig(QVector<QuickLaunchApp>& apps,
                                  bool& onlyIfNotRunning)
{
    QSettings settings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    const int size = settings.beginReadArray(QStringLiteral("quickLaunch/apps"));
    apps.clear();
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QuickLaunchApp app;
        app.name = settings.value(QStringLiteral("name")).toString();
        app.path = settings.value(QStringLiteral("path")).toString();
        if (!app.path.isEmpty())
            apps.append(app);
    }
    settings.endArray();
    onlyIfNotRunning = settings.value(QStringLiteral("quickLaunch/onlyIfNotRunning"), true).toBool();
}

static void saveQuickLaunchConfig(const QVector<QuickLaunchApp>& apps,
                                  bool onlyIfNotRunning)
{
    QSettings settings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    settings.beginWriteArray(QStringLiteral("quickLaunch/apps"));
    for (int i = 0; i < apps.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), apps[i].name);
        settings.setValue(QStringLiteral("path"), apps[i].path);
    }
    settings.endArray();
    settings.setValue(QStringLiteral("quickLaunch/onlyIfNotRunning"), onlyIfNotRunning);
}

void MainWindow::openQuickLaunchManager()
{
    loadQuickLaunchConfig(m_quickLaunchApps, m_quickLaunchOnlyIfNotRunning);
    const ApplyStyle::MainWindowTheme theme = m_mainWindowTheme;

    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("quickLaunchManagerDialog"));
    dlg.setWindowTitle(QStringLiteral("管理应用列表"));
    dlg.resize(520, 360);

    auto* mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    auto* list = new QListWidget(&dlg);
    list->setObjectName(QStringLiteral("quickLaunchAppList"));
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const auto& app : m_quickLaunchApps) {
        auto* item = new QListWidgetItem(list);
        item->setText(app.name.isEmpty() ? QFileInfo(app.path).completeBaseName() : app.name);
        item->setData(Qt::UserRole, app.name);
        item->setData(Qt::UserRole + 1, app.path);
    }
    mainLayout->addWidget(list, 1);

    auto* onlyRow = new QWidget(&dlg);
    auto* onlyLayout = new QHBoxLayout(onlyRow);
    onlyLayout->setContentsMargins(0, 0, 0, 0);
    onlyLayout->setSpacing(6);
    auto* onlyBox = new QCheckBox(QStringLiteral("只启动未运行的应用"), onlyRow);
    onlyBox->setObjectName(QStringLiteral("quickLaunchOnlyBox"));
    onlyBox->setChecked(m_quickLaunchOnlyIfNotRunning);
    auto* helpBtn = new QToolButton(onlyRow);
    helpBtn->setObjectName(QStringLiteral("quickLaunchHelpButton"));
    helpBtn->setAutoRaise(true);
    helpBtn->setIcon(QIcon(QStringLiteral(":/question_mark_icon.svg")));
    helpBtn->setToolTip(QStringLiteral("查看使用说明"));
    helpBtn->setCursor(Qt::PointingHandCursor);
    onlyLayout->addWidget(onlyBox);
    onlyLayout->addStretch(1);
    onlyLayout->addWidget(helpBtn);
    mainLayout->addWidget(onlyRow);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    auto* btnAdd = new QPushButton(QStringLiteral("添加应用..."), &dlg);
    btnAdd->setObjectName(QStringLiteral("quickLaunchAddButton"));
    auto* btnRemove = new QPushButton(QStringLiteral("删除选中"), &dlg);
    btnRemove->setObjectName(QStringLiteral("quickLaunchRemoveButton"));
    auto* btnOk = new QPushButton(QStringLiteral("确定"), &dlg);
    btnOk->setObjectName(QStringLiteral("quickLaunchOkButton"));
    auto* btnCancel = new QPushButton(QStringLiteral("取消"), &dlg);
    btnCancel->setObjectName(QStringLiteral("quickLaunchCancelButton"));
    btnRow->addWidget(btnAdd);
    btnRow->addWidget(btnRemove);
    btnRow->addSpacing(10);
    btnRow->addWidget(btnOk);
    btnRow->addWidget(btnCancel);
    mainLayout->addLayout(btnRow);

    connect(btnAdd, &QPushButton::clicked, &dlg, [&]() {
        const QString path = QFileDialog::getOpenFileName(
            &dlg,
            QStringLiteral("选择应用程序"),
            QString(),
            QStringLiteral("应用程序 (*.exe);;所有文件 (*.*)"));
        if (path.isEmpty())
            return;
        QFileInfo info(path);
        auto* item = new QListWidgetItem(list);
        item->setText(info.completeBaseName());
        item->setData(Qt::UserRole, info.completeBaseName());
        item->setData(Qt::UserRole + 1, path);
    });

    connect(btnRemove, &QPushButton::clicked, &dlg, [&]() {
        auto* item = list->currentItem();
        if (!item)
            return;
        delete item;
    });

    connect(btnOk, &QPushButton::clicked, &dlg, [&]() {
        m_quickLaunchApps.clear();
        for (int i = 0; i < list->count(); ++i) {
            auto* item = list->item(i);
            QuickLaunchApp app;
            app.name = item->data(Qt::UserRole).toString();
            app.path = item->data(Qt::UserRole + 1).toString();
            if (!app.path.isEmpty())
                m_quickLaunchApps.append(app);
        }
        m_quickLaunchOnlyIfNotRunning = onlyBox->isChecked();
        saveQuickLaunchConfig(m_quickLaunchApps, m_quickLaunchOnlyIfNotRunning);
        dlg.accept();
    });

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);

    connect(helpBtn, &QToolButton::clicked, &dlg, [&dlg, theme]() {
        const QString text = QStringLiteral(
            "【快速启动应用使用说明】\n\n"
            "1. 通过「添加应用...」选择常用程序的可执行文件（*.exe）。\n"
            "2. 「只启动未运行的应用」勾选后，每次快速启动只会启动当前未在运行的应用，避免重复打开多个实例。\n"
            "3. 快速启动入口位于主界面「快速启动应用」卡片：\n"
            "   - 左键：按列表依次启动应用；\n"
            "   - 右键：「管理应用列表」，可增删应用并修改该选项。\n"
            "4. 微信为特例，将按系统默认方式启动，其它应用则按各自默认窗口状态启动。\n");
        QMessageBox msgBox(&dlg);
        msgBox.setWindowTitle(QStringLiteral("快速启动应用 - 使用说明"));
        msgBox.setText(text);
        msgBox.setIconPixmap(QIcon(QStringLiteral(":/question_mark_icon.svg")).pixmap(32, 32));
        msgBox.setStandardButtons(QMessageBox::Close);
        msgBox.setStyleSheet(ApplyStyle::quickLaunchHelpMessageBoxStyle(theme));
        msgBox.exec();
    });

    dlg.setStyleSheet(ApplyStyle::quickLaunchManagerStyle(theme));
    dlg.exec();
}

static bool isProcessRunningByName(const QString& exeName)
{
    if (exeName.isEmpty())
        return false;
    QProcess proc;
    proc.start(QStringLiteral("cmd"),
               {QStringLiteral("/c"),
                QStringLiteral("tasklist /FI \"IMAGENAME eq %1\" /NH").arg(exeName)});
    if (!proc.waitForFinished(2000))
        return false;
    const QByteArray out = proc.readAllStandardOutput().toLower();
    return out.contains(exeName.toLower().toLatin1());
}

void MainWindow::runQuickLaunchApps()
{
    loadQuickLaunchConfig(m_quickLaunchApps, m_quickLaunchOnlyIfNotRunning);
    if (m_quickLaunchApps.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("尚未配置要快速启动的应用，右键「快速启动应用」可进行管理。"), 5000);
        return;
    }

    for (const auto& app : m_quickLaunchApps) {
        if (app.path.isEmpty())
            continue;
        QFileInfo info(app.path);
        const QString exeName = info.fileName();
        if (m_quickLaunchOnlyIfNotRunning && isProcessRunningByName(exeName))
            continue;
        // 直接启动可执行文件，避免通过 cmd/start 时因路径中空格或括号导致解析错误
        const QString program = info.absoluteFilePath();
        const QString workDir = info.absolutePath();
        QProcess::startDetached(program, QStringList(), workDir);
    }

    statusBar()->showMessage(QStringLiteral("已尝试启动配置的应用。"), 5000);
}

// ==================== Help Dialog ====================

class HelpDialog : public QDialog
{
public:
    explicit HelpDialog(const QString& initialSection, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("帮助中心"));
        setMinimumSize(600, 400);
        resize(780, 540);

        auto* root = new QHBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->setChildrenCollapsible(false);
        root->addWidget(splitter);

        m_toc = new QListWidget;
        m_toc->setObjectName("helpToc");
        m_toc->setFixedWidth(170);
        splitter->addWidget(m_toc);

        m_browser = new QTextBrowser;
        m_browser->setObjectName("helpBrowser");
        m_browser->setOpenExternalLinks(false);
        m_browser->setOpenLinks(false);
        splitter->addWidget(m_browser);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({170, 610});

        populateContent();

        connect(m_toc, &QListWidget::currentItemChanged, this,
                [this](QListWidgetItem* current, QListWidgetItem*) {
                    if (!current) return;
                    const QString anchor = current->data(Qt::UserRole).toString();
                    if (!anchor.isEmpty())
                        m_browser->scrollToAnchor(anchor);
                });

        setStyleSheet(
            ApplyStyle::helpDialogStyle(ApplyStyle::loadSavedMainWindowTheme()));

        QListWidgetItem* startItem = nullptr;
        for (int i = 0; i < m_toc->count(); ++i) {
            auto* item = m_toc->item(i);
            const QString anchor = item->data(Qt::UserRole).toString();
            if (anchor.isEmpty()) continue;
            if (!startItem) startItem = item;
            if (initialSection == QLatin1String("buglog")
                && anchor.startsWith(QLatin1String("bug"))) {
                startItem = item;
                break;
            }
        }
        if (startItem)
            m_toc->setCurrentItem(startItem);
    }

private:
    QListWidget* m_toc = nullptr;
    QTextBrowser* m_browser = nullptr;

    struct Section {
        QString anchor;
        QString tocLabel;
        QString html;
    };

    void populateContent()
    {
        QVector<Section> sections;
        const ApplyStyle::MainWindowTheme theme = ApplyStyle::loadSavedMainWindowTheme();

        // ---- 使用说明 ----
        sections.append({
            "help_overview", QStringLiteral("\u2022 基本操作"),
            QStringLiteral(
                "<h3>基本操作</h3>"
                "<p>1. 左侧「在线平台」「客服平台」列表用于选择和管理各个平台窗口。</p>"
                "<p>2. 点击顶部「添加新窗口」可从当前已打开的窗口中选择并关联到平台项。</p>"
                "<p>3. 支持多选窗口批量添加，添加过程中会显示进度提示。</p>")
        });
        sections.append({
            "help_aggregate", QStringLiteral("\u2022 聚合接待"),
            QStringLiteral(
                "<h3>聚合接待</h3>"
                "<p>管理后台中的「聚合接待」用于统一查看各平台会话并手动回复客户。</p>"
                "<p>点击左侧「聚合接待」即可进入，浮窗窗口会自动隐藏，切回平台项后自动恢复。</p>")
        });
        sections.append({
            "help_disconnect", QStringLiteral("\u2022 断开窗口关联"),
            QStringLiteral(
                "<h3>断开窗口关联</h3>"
                "<p>在左侧「在线平台」或「客服平台」列表中，<b>右键</b>点击对应平台项，选择「断开关联」或「删除」即可断开与主窗口的关联（不会关闭该应用窗口本身）。</p>"
                "<p style='color:%1;'>&#9888; 请勿使用嵌入窗口标题栏上的「最小化」「最大化」「关闭」按钮，可能引发异常或白屏。需断开关联时请通过上述右键菜单操作。</p>")
                .arg(ApplyStyle::helpDialogHtmlWarningColor(theme))
        });
        sections.append({
            "help_quicklaunch", QStringLiteral("\u2022 快速启动应用"),
            QStringLiteral(
                "<h3>快速启动应用</h3>"
                "<p>主界面中部的「快速启动应用」卡片，可一键启动常用外部应用。</p>"
                "<p><b>左键</b>：按列表依次启动应用。</p>"
                "<p><b>右键</b>：「管理应用列表」，可增删应用并修改启动选项。</p>"
                "<p>微信为特例，将按系统默认方式启动，其它应用则按各自默认窗口状态启动。</p>")
        });
        sections.append({
            "help_troubleshoot", QStringLiteral("\u2022 故障排查"),
            QStringLiteral(
                "<h3>故障排查</h3>"
                "<p>若遇到窗口嵌入、浮窗跟随或快速启动异常，可从状态栏日志或控制台查看详细信息。</p>")
        });

        // ---- Bug 修复日志 ----
        sections.append({
            "bug_float_style", QStringLiteral("\u2022 浮窗样式异常"),
            QStringLiteral(
                "<h3>浮窗跟随窗口样式异常</h3>"
                "<p><b>问题：</b>部分外部窗口被修改了原始样式（标题栏、边框丢失或恢复异常）。</p>"
                "<p><b>原因：</b>在 setupFloatFollow / detachFloatFollow 中直接改写了窗口的样式标志。</p>"
                "<p><b>修复：</b>仅调整必要的扩展样式和 Owner，不再强制改写 WS_OVERLAPPEDWINDOW 等样式。</p>")
        });
        sections.append({
            "bug_disconnect", QStringLiteral("\u2022 窗口断开关联"),
            QStringLiteral(
                "<h3>外部窗口断开关联</h3>"
                "<p><b>问题：</b>直接点击外部窗口（如微信）自身的关闭按钮时，可能出现白屏、列表未移除等异常。</p>"
                "<p><b>原因：</b>外部窗口关闭行为因应用和系统环境差异大（隐藏到托盘、白屏挂起等），无法统一可靠检测。</p>"
                "<p><b>建议：</b>请通过左侧平台列表「右键 &rarr; 断开关联 / 删除」来安全释放窗口。</p>")
        });
        sections.append({
            "bug_batch_add", QStringLiteral("\u2022 多选与进度提示"),
            QStringLiteral(
                "<h3>\"添加新窗口\"多选与进度提示</h3>"
                "<p><b>问题：</b>一次只能添加一个窗口，重复操作繁琐；添加过程缺乏进度反馈。</p>"
                "<p><b>修复：</b>支持列表多选加入队列逐个添加，并增加遮罩 + 进度文本提示。</p>")
        });
        sections.append({
            "bug_enum_noise", QStringLiteral("\u2022 枚举噪声过滤"),
            QStringLiteral(
                "<h3>顶层窗口枚举噪声</h3>"
                "<p><b>问题：</b>窗口列表中出现系统输入法等辅助窗口，容易误选。</p>"
                "<p><b>原因：</b>枚举顶层窗口时仅按可见和 Owner 过滤，未按进程/标题进一步排除。</p>"
                "<p><b>修复：</b>对 TextInputHost.exe 进程以及标题包含\"Windows 输入体验\"的窗口进行过滤。</p>")
        });
        sections.append({
            "bug_aggregate_ui", QStringLiteral("\u2022 聚合接待界面优化"),
            QStringLiteral(
                "<h3>聚合接待界面视觉与体验</h3>"
                "<p><b>问题：</b>早期版本风格偏暗，缺少渐变和明确分区，消息区域滚动条和气泡样式不统一。</p>"
                "<p><b>修复：</b>采用柔和蓝色渐变背景，统一三栏布局；调整消息气泡配色、隐藏滚动条、统一文字为黑色，并对空态/搜索框做了细致优化。</p>")
        });
        sections.append({
            "bug_quicklaunch", QStringLiteral("\u2022 快速启动路径修复"),
            QStringLiteral(
                "<h3>快速启动应用功能</h3>"
                "<p><b>问题：</b>最初使用 cmd/start 启动时，在包含空格或括号的路径下会出现\"找不到\"之类错误提示。</p>"
                "<p><b>原因：</b>命令行参数拼接方式不当，导致 Windows 对带空格路径解析失败。</p>"
                "<p><b>修复：</b>改为直接使用 QProcess::startDetached 启动 exe，并增加「只启动未运行的应用」选项。</p>")
        });

        QString html = QStringLiteral(
            "<html><body style='font-family: \"Microsoft YaHei\", \"Segoe UI\", sans-serif; "
            "font-size: 13px; color: %1; margin: 0; padding: 16px 16px 16px 0;'>")
                            .arg(ApplyStyle::helpDialogHtmlBodyTextColor(theme));

        bool helpGroupAdded = false;
        bool bugGroupAdded = false;

        for (const auto& s : sections) {
            if (!helpGroupAdded && s.anchor.startsWith(QLatin1String("help"))) {
                helpGroupAdded = true;
                auto* header = new QListWidgetItem(QStringLiteral("  软件使用说明"));
                header->setFlags(Qt::NoItemFlags);
                m_toc->addItem(header);
            }
            if (!bugGroupAdded && s.anchor.startsWith(QLatin1String("bug"))) {
                bugGroupAdded = true;
                auto* header = new QListWidgetItem(QStringLiteral("  Bug 修复日志"));
                header->setFlags(Qt::NoItemFlags);
                m_toc->addItem(header);
            }

            auto* item = new QListWidgetItem(s.tocLabel);
            item->setData(Qt::UserRole, s.anchor);
            m_toc->addItem(item);

            html += QStringLiteral("<a name=\"%1\"></a>%2<hr style='border: none; "
                                   "border-top: 1px solid %3; margin: 18px 0;'>")
                        .arg(s.anchor, s.html, ApplyStyle::helpDialogHtmlHrBorderColor(theme));
        }

        html += QStringLiteral("</body></html>");
        m_browser->setHtml(html);
        m_browser->document()->setDocumentMargin(0);
    }
};

// ==================== Help / Bug dialogs ====================

void MainWindow::openAppHelpDialog()
{
    HelpDialog dlg(QStringLiteral("help"), this);
    dlg.exec();
}

void MainWindow::openBugLogDialog()
{
    HelpDialog dlg(QStringLiteral("buglog"), this);
    dlg.exec();
}

void MainWindow::startBatchAddWindows(const QVector<WindowInfo>& list)
{
    if (list.isEmpty()) return;

    m_batchAddIndex = 0;
    m_batchAddList = list;
    m_batchAddIndex = 0;
    m_batchAddSuccessCount = 0;

    if (!m_batchAddOverlay) {
        m_batchAddOverlay = new BatchAddOverlayWidget(nullptr);
        auto* layout = new QVBoxLayout(m_batchAddOverlay);
        layout->setAlignment(Qt::AlignCenter);
        layout->setContentsMargins(24, 24, 24, 24);
        m_batchAddPrompt = new QLabel(m_batchAddOverlay);
        m_batchAddPrompt->setObjectName("batchAddPrompt");
        m_batchAddPrompt->setStyleSheet(
            "QLabel#batchAddPrompt { background-color: rgba(255, 255, 255, 0.95); "
            "color: #333; font-size: 16px; padding: 20px 32px; border-radius: 12px; }");
        m_batchAddPrompt->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_batchAddPrompt, 0, Qt::AlignHCenter);
    }

    QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    m_batchAddOverlay->setGeometry(screenRect);
    m_batchAddPrompt->setText(QStringLiteral("正在添加 0/%1...").arg(list.size()));
    m_batchAddOverlay->show();
    m_batchAddOverlay->raise();
    m_batchAddOverlay->activateWindow();

    QTimer::singleShot(80, this, &MainWindow::processNextBatchAdd);
}

void MainWindow::processNextBatchAdd()
{
    if (m_batchAddIndex >= m_batchAddList.size()) {
        if (!m_batchAddOverlay || !m_batchAddPrompt) return;
        m_batchAddPrompt->setText(QStringLiteral("共 %1 个，成功添加 %2 个")
                                      .arg(m_batchAddList.size()).arg(m_batchAddSuccessCount));
        const int total = m_batchAddList.size();
        const int success = m_batchAddSuccessCount;
        m_batchAddList.clear();
        m_batchAddIndex = -1;
        m_batchAddSuccessCount = 0;
        QTimer::singleShot(2000, this, [this, total, success]() {
            if (m_batchAddIndex < 0 && m_batchAddOverlay) {
                m_batchAddOverlay->deleteLater();
                m_batchAddOverlay = nullptr;
                m_batchAddPrompt = nullptr;
            }
            if (success == 0)
                showSystemReadyPage();
        });
        return;
    }

    const WindowInfo& info = m_batchAddList.at(m_batchAddIndex);
    bool ok = false;
    if (info.handle != 0 && Win32WindowHelper::isWindowValid(info.handle)) {
        addWindowToPlatform(info);
        ok = true;
        ++m_batchAddSuccessCount;
    }
    ++m_batchAddIndex;

    if (m_batchAddPrompt) {
        m_batchAddPrompt->setText(QStringLiteral("正在添加 %1/%2...")
                                      .arg(m_batchAddIndex).arg(m_batchAddList.size()));
    }
    QTimer::singleShot(ok ? 120 : 50, this, &MainWindow::processNextBatchAdd);
}

// ==================== Window Management ====================

WindowDisplayMode MainWindow::determineDisplayMode(const WindowInfo& info)
{
    QString proc = info.processName.toLower();
    if (proc.contains("aliworkbench")
        || proc.contains("aliim") || proc.contains("qianniu"))
        return WindowDisplayMode::Embed;
    return WindowDisplayMode::FloatFollow;
}

bool MainWindow::isWechatWindowInfo(const WindowInfo& info)
{
    const QString proc = info.processName.toLower();
    const QString title = info.platformName.toLower();
    return proc.contains(QStringLiteral("wechat"))
           || title.contains(QStringLiteral("wechat"))
           || info.platformName.contains(QStringLiteral("微信"));
}

QSet<quintptr> MainWindow::managedWindowHandles() const
{
    QSet<quintptr> set;
    for (auto it = m_managedWindows.constBegin(); it != m_managedWindows.constEnd(); ++it)
        set.insert(it.value().handle);
    return set;
}

QString MainWindow::matchCustomerServicePlatform(const WindowInfo& info) const
{
    if (info.platformName.contains(QStringLiteral("千牛"))) return QStringLiteral("qianniu");
    if (info.platformName.contains(QStringLiteral("拼多多"))) return QStringLiteral("pinduoduo");
    if (info.platformName.contains(QStringLiteral("抖店"))) return QStringLiteral("douyin");

    QString proc = info.processName.toLower();
    if (proc.contains("aliworkbench") || proc.contains("aliim") || proc.contains("qianniu"))
        return QStringLiteral("qianniu");
    if (proc.contains("pinduoduo") || proc.contains("pdd"))
        return QStringLiteral("pinduoduo");
    if (proc.contains("douyin") || proc.contains("feige") || proc.contains("jinritemai"))
        return QStringLiteral("douyin");

    return {};
}

QStandardItem* MainWindow::findGroupItem(const QString& groupId) const
{
    for (int i = 0; i < m_platformTreeModel->rowCount(); ++i) {
        auto* item = m_platformTreeModel->item(i);
        if (item && item->data(PlatformIdRole).toString() == groupId)
            return item;
    }
    return nullptr;
}

QStandardItem* MainWindow::findChildItem(QStandardItem* parent, const QString& platformId) const
{
    if (!parent) return nullptr;
    for (int i = 0; i < parent->rowCount(); ++i) {
        auto* child = parent->child(i);
        if (child && child->data(PlatformIdRole).toString() == platformId)
            return child;
    }
    return nullptr;
}

void MainWindow::addWindowToPlatform(const WindowInfo& info)
{
    WindowDisplayMode mode = determineDisplayMode(info);
    QString csMatch = matchCustomerServicePlatform(info);
    bool isCS = !csMatch.isEmpty();
    QString platformId;

    if (isCS) {
        platformId = csMatch;
        // If already has a window, detach old one first
        if (m_managedWindows.contains(platformId)) {
            auto& old = m_managedWindows[platformId];
            if (old.wasSetup) {
                if (old.mode == WindowDisplayMode::Embed)
                    Win32WindowHelper::detachWindow(old.handle);
                else
                    Win32WindowHelper::detachFloatFollow(old.handle,
                                                         true,
                                                         old.useFloatToolWindow,
                                                         old.useFloatOwner);
            }
            if (old.stackPage) {
                m_centerStack->removeWidget(old.stackPage);
                old.stackPage->deleteLater();
            }
            m_managedWindows.remove(platformId);
        }
        // Update tree item to activated
        auto* csItem = findChildItem(m_csGroup, platformId);
        if (csItem) {
            csItem->setData(true, IsActivatedRole);
        }
        // Update group dot to green if at least one is activated
        m_csGroup->setData(QColor(82, 196, 26), DotColorRole);
        qInfo() << "[MainWindow] 客服平台关联:" << platformId << "<-" << info.platformName;
    } else {
        platformId = QStringLiteral("online_%1").arg(m_nextOnlineId++);

        // Add to tree under online group
        QIcon icon = Win32WindowHelper::windowIcon(info);
        if (icon.isNull())
            icon = onlinePlatformFallbackIcon(info);
        if (icon.isNull())
            icon = qApp->style()->standardIcon(QStyle::SP_ComputerIcon);
        auto* item = new QStandardItem(icon, info.platformName);
        item->setData(platformId, PlatformIdRole);
        item->setData(false, IsGroupRole);
        item->setData(false, IsCustomerServiceItemRole);
        item->setData(true, IsActivatedRole);
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        m_onlineGroup->appendRow(item);
        m_platformTree->expand(m_onlineGroup->index());
        qInfo() << "[MainWindow] 在线平台添加:" << platformId << "=" << info.platformName;
    }

    // Create stack page
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("page_%1").arg(platformId));
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    EmbeddedWindowContainer* container = nullptr;
    if (mode == WindowDisplayMode::Embed) {
        container = new EmbeddedWindowContainer(page);
        container->setObjectName("embedContainer");
        pageLayout->addWidget(container);
    } else {
        auto* floatLabel = new QLabel(page);
        floatLabel->setAlignment(Qt::AlignCenter);
        floatLabel->setText(QStringLiteral("浮窗模式 — %1").arg(info.platformName));
        floatLabel->setStyleSheet("color: #999; font-size: 14px;");
        pageLayout->addWidget(floatLabel);
    }
    m_centerStack->addWidget(page);

    // Store entry
    ManagedWindowEntry entry;
    entry.platformName = info.platformName;
    entry.platformId = platformId;
    entry.handle = info.handle;
    entry.mode = mode;
    entry.isCustomerService = isCS;
    if (mode == WindowDisplayMode::FloatFollow && isWechatWindow(info)) {
        entry.useFloatOwner = false;
        entry.useFloatToolWindow = false;
        entry.useFloatRaiseAbove = true;
    }
    entry.container = container;
    entry.stackPage = page;
    entry.wasSetup = false;
    m_managedWindows[platformId] = entry;

    updateTreeViewHeight();

    // Auto-select the newly added item
    QStandardItem* targetItem = nullptr;
    if (isCS) {
        targetItem = findChildItem(m_csGroup, platformId);
    } else {
        targetItem = findChildItem(m_onlineGroup, platformId);
    }
    if (targetItem) {
        m_platformTree->setCurrentIndex(targetItem->index());
    }
}

void MainWindow::switchToWindow(const QString& platformId)
{
    if (!m_managedWindows.contains(platformId)) return;
    auto& entry = m_managedWindows[platformId];

    if (!Win32WindowHelper::isWindowValid(entry.handle)) {
        qWarning() << "[MainWindow] 窗口已失效:" << entry.platformName;
        removeOnlinePlatformItem(platformId);
        return;
    }

    {
        const QRect windowRect = Win32WindowHelper::windowRect(entry.handle);
        const unsigned int dpi = Win32WindowHelper::windowDpi(entry.handle);
        qInfo() << "[MainWindow] 切换外部窗口:"
                << "platformId=" << platformId
                << "name=" << entry.platformName
                << "mode=" << (entry.mode == WindowDisplayMode::Embed ? "Embed" : "FloatFollow")
                << "handle=0x" << QString::number(static_cast<qulonglong>(entry.handle), 16)
                << "dpi=" << dpi
                << "windowRect(" << formatRect(windowRect) << ")";
    }

    // Hide current float if switching away
    hideCurrentFloatWindow();

    // First-time setup
    if (!entry.wasSetup) {
        if (entry.mode == WindowDisplayMode::Embed && entry.container) {
            auto* ec = static_cast<EmbeddedWindowContainer*>(entry.container);
            ec->setEmbeddedHandle(entry.handle);
            qInfo() << "[MainWindow] 嵌入窗口:" << entry.platformName;
        } else if (entry.mode == WindowDisplayMode::FloatFollow) {
            Win32WindowHelper::setupFloatFollow(entry.handle,
                                                (quintptr)winId(),
                                                entry.useFloatOwner,
                                                entry.useFloatToolWindow);
            qInfo() << "[MainWindow] 浮窗跟随设置:"
                    << "name=" << entry.platformName
                    << "useOwner=" << entry.useFloatOwner
                    << "useToolWindow=" << entry.useFloatToolWindow
                    << "raiseAbove=" << entry.useFloatRaiseAbove;
        }
        entry.wasSetup = true;
    }

    // Switch stack
    m_centerStack->setCurrentWidget(entry.stackPage);
    m_activeWindowId = platformId;

    // Ensure embedded window填满 CenterContent（解决首次嵌入宽高为 0 的问题）
    if (entry.mode == WindowDisplayMode::Embed && entry.container && entry.wasSetup) {
        QTimer::singleShot(0, this, [this, platformId]() {
            if (!m_managedWindows.contains(platformId)) return;
            auto& currentEntry = m_managedWindows[platformId];
            if (currentEntry.handle && currentEntry.container) {
                Win32WindowHelper::resizeEmbeddedWindow(currentEntry.handle, currentEntry.container);
                QPoint topLeft = currentEntry.container->mapToGlobal(QPoint(0, 0));
                currentEntry.lastDisplayGeometry = QRect(topLeft, currentEntry.container->size());
            }
        });
    }

    // Show float window if needed
    if (entry.mode == WindowDisplayMode::FloatFollow) {
        updateFloatFollowPosition();
    }
}

void MainWindow::hideCurrentFloatWindow()
{
    if (m_activeWindowId.isEmpty()) return;
    if (!m_managedWindows.contains(m_activeWindowId)) return;

    auto& entry = m_managedWindows[m_activeWindowId];
    if (entry.mode == WindowDisplayMode::FloatFollow && entry.wasSetup) {
        // For WeChat: don't SW_HIDE, keep it renderable for PrintWindow-based OCR.
        // Move it far off-screen instead (still non-activated).
        if (entry.platformName.contains(QStringLiteral("微信"))) {
            const int w = entry.lastDisplayGeometry.isValid() ? entry.lastDisplayGeometry.width() : 800;
            const int h = entry.lastDisplayGeometry.isValid() ? entry.lastDisplayGeometry.height() : 600;
            Win32WindowHelper::showWindowAt(entry.handle, -20000, -20000, w, h, false);
        } else {
            Win32WindowHelper::hideWindow(entry.handle);
        }
    }
}

void MainWindow::checkManagedWindowsState()
{
    if (m_managedWindows.isEmpty()) {
        return;
    }

    QStringList invalidPlatformIds;
    for (auto it = m_managedWindows.cbegin(); it != m_managedWindows.cend(); ++it) {
        if (!Win32WindowHelper::isWindowValid(it.value().handle)) {
            invalidPlatformIds.append(it.key());
        }
    }

    if (invalidPlatformIds.isEmpty() && !m_activeWindowId.isEmpty()
        && m_managedWindows.contains(m_activeWindowId)) {
        auto& activeEntry = m_managedWindows[m_activeWindowId];
        const bool isInvisible = !Win32WindowHelper::isWindowVisible(activeEntry.handle);
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const bool usesCloseIntentDetection = activeEntry.mode == WindowDisplayMode::FloatFollow;

        if (usesCloseIntentDetection && activeEntry.wasSetup) {
            const QRect currentRect = displayRectForEntry(activeEntry);
            const QPoint cursorPos = QCursor::pos();
            const bool inCloseHotspot = isInWindowCloseHotspot(currentRect, cursorPos);
            const bool overManagedWindow = Win32WindowHelper::isPointInsideWindow(activeEntry.handle, cursorPos);
            const bool leftButtonPressed = Win32WindowHelper::isLeftMouseButtonPressed();
            if (!isInvisible && inCloseHotspot && overManagedWindow && leftButtonPressed) {
                if (activeEntry.closeIntentSinceMs == 0) {
                    activeEntry.closeIntentSinceMs = nowMs;
                    if (m_centerStack->currentWidget() == activeEntry.stackPage) {
                        showSystemReadyPage();
                    }
                }
            } else if (activeEntry.closeIntentSinceMs > 0
                       && (nowMs - activeEntry.closeIntentSinceMs) > 1500) {
                activeEntry.closeIntentSinceMs = 0;
            }
        }

        if (!isInvisible) {
            activeEntry.invisibleSinceMs = 0;
        } else if (activeEntry.invisibleSinceMs == 0) {
            activeEntry.invisibleSinceMs = nowMs;
        }

        const bool invisibleForLongEnough = activeEntry.invisibleSinceMs > 0
                                            && (nowMs - activeEntry.invisibleSinceMs) >= 600;
        const bool invisibleAfterCloseIntent = usesCloseIntentDetection
                                               && activeEntry.closeIntentSinceMs > 0
                                               && activeEntry.invisibleSinceMs > 0
                                               && activeEntry.invisibleSinceMs >= activeEntry.closeIntentSinceMs
                                               && (nowMs - activeEntry.closeIntentSinceMs) <= 1500;

        const qint64 elapsedSinceCloseIntent = activeEntry.closeIntentSinceMs > 0
                                                   ? (nowMs - activeEntry.closeIntentSinceMs) : 0;
        const bool gracePeriodElapsed = usesCloseIntentDetection
                                        && activeEntry.closeIntentSinceMs > 0
                                        && elapsedSinceCloseIntent >= 800
                                        && isInvisible;

        const bool shouldDisconnect = usesCloseIntentDetection
                                          ? (invisibleAfterCloseIntent || gracePeriodElapsed)
                                          : invisibleForLongEnough;

        if (activeEntry.wasSetup && shouldDisconnect) {
            invalidPlatformIds.append(m_activeWindowId);
        }
    }

    if (invalidPlatformIds.isEmpty()) {
        return;
    }

    invalidPlatformIds.removeDuplicates();
    for (const QString& platformId : invalidPlatformIds) {
        if (!m_managedWindows.contains(platformId)) {
            continue;
        }

        const QString platformName = m_managedWindows[platformId].platformName;
        qInfo() << "[MainWindow] 检测到外部窗口已关闭或隐藏，自动断开:" << platformId << platformName;
        removeOnlinePlatformItem(platformId, false, false);
        statusBar()->showMessage(QStringLiteral("已检测到“%1”窗口关闭或隐藏，已自动断开关联")
                                     .arg(platformName),
                                 3000);
    }
}

void MainWindow::updateFloatFollowPosition()
{
    if (m_activeWindowId.isEmpty()) return;
    if (!m_managedWindows.contains(m_activeWindowId)) return;

    auto& entry = m_managedWindows[m_activeWindowId];
    if (entry.mode != WindowDisplayMode::FloatFollow) return;
    if (!entry.wasSetup) return;
    if (entry.closeIntentSinceMs > 0) return;

    if (isMinimized() || m_centerStack->currentWidget() != entry.stackPage) {
        // For WeChat: keep window renderable for background OCR (PrintWindow).
        if (entry.platformName.contains(QStringLiteral("微信"))) {
            const int w = entry.lastDisplayGeometry.isValid() ? entry.lastDisplayGeometry.width() : 800;
            const int h = entry.lastDisplayGeometry.isValid() ? entry.lastDisplayGeometry.height() : 600;
            Win32WindowHelper::showWindowAt(entry.handle, -20000, -20000, w, h, false);
        } else {
            Win32WindowHelper::hideWindow(entry.handle);
        }
        return;
    }

    QPoint logicalTopLeft = m_centerStack->mapToGlobal(QPoint(0, 0));
    QSize logicalSize = m_centerStack->size();
    QScreen* screen = m_centerStack->screen();
    if (!screen && windowHandle()) {
        screen = windowHandle()->screen();
    }

    const qreal scale = screen ? screen->devicePixelRatio() : 1.0;
    const QRect targetWindowRect(qRound(logicalTopLeft.x() * scale),
                                 qRound(logicalTopLeft.y() * scale),
                                 qRound(logicalSize.width() * scale),
                                 qRound(logicalSize.height() * scale));

    entry.lastDisplayGeometry = targetWindowRect;
    entry.invisibleSinceMs = 0;
    entry.closeIntentSinceMs = 0;
    {
        const QRect windowRect = Win32WindowHelper::windowRect(entry.handle);
        const unsigned int dpi = Win32WindowHelper::windowDpi(entry.handle);
        qInfo() << "[MainWindow] 浮窗跟随定位:"
                << "platformId=" << m_activeWindowId
                << "name=" << entry.platformName
                << "handle=0x" << QString::number(static_cast<qulonglong>(entry.handle), 16)
                << "dpi=" << dpi
                << "screenScale=" << scale
                << "centerStackLogical(" << formatRect(QRect(logicalTopLeft, logicalSize)) << ")"
                << "targetRect(" << formatRect(targetWindowRect) << ")"
                << "windowRectBefore(" << formatRect(windowRect) << ")";
    }
    Win32WindowHelper::showWindowAt(entry.handle,
                                    entry.lastDisplayGeometry.x(),
                                    entry.lastDisplayGeometry.y(),
                                    entry.lastDisplayGeometry.width(),
                                    entry.lastDisplayGeometry.height(),
                                    entry.useFloatRaiseAbove);
}

QRect MainWindow::displayRectForEntry(const ManagedWindowEntry& entry) const
{
    if (entry.mode == WindowDisplayMode::Embed && entry.container && entry.container->isVisible()) {
        QPoint topLeft = entry.container->mapToGlobal(QPoint(0, 0));
        return QRect(topLeft, entry.container->size());
    }

    if (entry.mode == WindowDisplayMode::FloatFollow && entry.wasSetup
        && m_activeWindowId == entry.platformId) {
        QRect rect = Win32WindowHelper::windowRect(entry.handle);
        if (rect.isValid()) {
            return rect;
        }
    }

    return entry.lastDisplayGeometry;
}

void MainWindow::releaseManagedWindow(ManagedWindowEntry& entry,
                                      bool keepVisible,
                                      bool showWindowAfterRelease)
{
    if (!entry.wasSetup) return;

    const QRect targetRect = displayRectForEntry(entry);
    const bool isWechatSpecialFloat = entry.mode == WindowDisplayMode::FloatFollow
                                      && !entry.useFloatOwner
                                      && !entry.useFloatToolWindow;

    if (entry.mode == WindowDisplayMode::Embed) {
        Win32WindowHelper::detachWindow(entry.handle, targetRect);
    } else if (isWechatSpecialFloat) {
        if (showWindowAfterRelease && keepVisible && targetRect.isValid()) {
            Win32WindowHelper::showWindowAt(entry.handle,
                                            targetRect.x(),
                                            targetRect.y(),
                                            targetRect.width(),
                                            targetRect.height(),
                                            entry.useFloatRaiseAbove);
        } else if (!showWindowAfterRelease) {
            Win32WindowHelper::hideWindow(entry.handle);
        }
    } else {
        const bool restoreTaskbarEntry = entry.useFloatToolWindow
                                             ? showWindowAfterRelease
                                             : true;
        Win32WindowHelper::detachFloatFollow(entry.handle,
                                             showWindowAfterRelease,
                                             restoreTaskbarEntry,
                                             entry.useFloatOwner);
        if (keepVisible && showWindowAfterRelease && targetRect.isValid()) {
            Win32WindowHelper::showWindowAt(entry.handle, targetRect.x(), targetRect.y(),
                                            targetRect.width(), targetRect.height());
        }
    }

    if (!keepVisible && showWindowAfterRelease) {
        Win32WindowHelper::minimizeWindow(entry.handle);
    }
}

void MainWindow::removeOnlinePlatformItem(const QString& platformId,
                                          bool keepVisible,
                                          bool showWindowAfterRelease)
{
    if (!m_managedWindows.contains(platformId)) return;
    auto& entry = m_managedWindows[platformId];

    const bool wasActive = (m_activeWindowId == platformId);
    if (wasActive) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
    }

    releaseManagedWindow(entry, keepVisible, showWindowAfterRelease);

    // Remove stack page
    if (entry.stackPage) {
        m_centerStack->removeWidget(entry.stackPage);
        entry.stackPage->deleteLater();
    }

    // Remove tree item
    if (entry.isCustomerService) {
        auto* csItem = findChildItem(m_csGroup, platformId);
        if (csItem) csItem->setData(false, IsActivatedRole);
    } else {
        auto* item = findChildItem(m_onlineGroup, platformId);
        if (item) m_onlineGroup->removeRow(item->row());
    }

    m_managedWindows.remove(platformId);
    updateTreeViewHeight();
    // 删除「当前正在显示」的托管项时，removeRow 会先触发一次 currentChanged；
    // 若此处再无条件 showSystemReadyPage()，会覆盖已切到的下一项（如微信）的 stack，
    // 造成树选中与 m_centerStack、m_activeWindowId 不一致，随后 move 时
    // updateFloatFollowPosition 会把 FloatFollow 微信误判为「非当前页」而挪到屏外。
    // 在数据与树结构都更新完后，再按当前选中项统一同步一次中间区域（幂等）。
    if (wasActive)
        onPlatformTreeSelectionChanged();
    qInfo() << "[MainWindow] 移除平台:" << platformId;
}

void MainWindow::detachAllWindows()
{
    for (auto it = m_managedWindows.begin(); it != m_managedWindows.end(); ++it) {
        auto& entry = it.value();
        if (!entry.wasSetup) continue;
        const bool keepVisible = (entry.platformId == m_activeWindowId);
        releaseManagedWindow(entry, keepVisible);
    }
    m_managedWindows.clear();
    m_activeWindowId.clear();
}

// ==================== Context Menu ====================

void MainWindow::showPlatformContextMenu(const QPoint& pos)
{
    QModelIndex idx = m_platformTree->indexAt(pos);
    if (!idx.isValid()) return;

    QString id = idx.data(PlatformIdRole).toString();
    bool isCS = idx.data(IsCustomerServiceItemRole).toBool();
    bool isGroup = idx.data(IsGroupRole).toBool();
    if (isGroup) return;

    if (!m_managedWindows.contains(id)) return;

    QMenu menu(this);
    if (isCS) {
        QAction* actDisconnect = menu.addAction(QStringLiteral("断开关联"));
        QAction* chosen = menu.exec(m_platformTree->viewport()->mapToGlobal(pos));
        if (chosen == actDisconnect) {
            removeOnlinePlatformItem(id);
        }
    } else {
        QAction* actRemove = menu.addAction(QStringLiteral("删除"));
        QAction* actCalibrateWechat = nullptr;
        {
            const QString name = m_managedWindows[id].platformName;
            if (name.contains(QStringLiteral("微信"))) {
                actCalibrateWechat = menu.addAction(QStringLiteral("微信OCR校准（备用方案）"));
            }
        }
        QAction* chosen = menu.exec(m_platformTree->viewport()->mapToGlobal(pos));
        if (chosen == actRemove) {
            removeOnlinePlatformItem(id);
        } else if (actCalibrateWechat && chosen == actCalibrateWechat) {
            startWechatRpaCalibration(id);
        }
    }
}

void MainWindow::startWechatRpaCalibration(const QString& platformId)
{
    if (!m_managedWindows.contains(platformId))
        return;

    auto& entry = m_managedWindows[platformId];
    if (!Win32WindowHelper::isWindowValid(entry.handle)) {
        qWarning() << "[MainWindow] 微信RPA校准失败：窗口无效";
        return;
    }

    // Ensure we are showing the correct page so user can see selection area
    switchToWindow(platformId);

    // Compute the same target rect used by float-follow so we can place overlay above it.
    QPoint logicalTopLeft = m_centerStack->mapToGlobal(QPoint(0, 0));
    QSize logicalSize = m_centerStack->size();
    QScreen* screen = m_centerStack->screen();
    if (!screen && windowHandle())
        screen = windowHandle()->screen();
    const qreal scale = screen ? screen->devicePixelRatio() : 1.0;

    const QRect targetRectPx(qRound(logicalTopLeft.x() * scale),
                             qRound(logicalTopLeft.y() * scale),
                             qRound(logicalSize.width() * scale),
                             qRound(logicalSize.height() * scale));
    const QRect targetRectLogical(logicalTopLeft, logicalSize);

    // Top-level overlay window stays above external float window.
    auto* overlay = new WechatCalibrationOverlay(nullptr);
    overlay->setGeometry(targetRectLogical);
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
    overlay->setFocus();

    qInfo() << "[MainWindow] 启动微信RPA校准：请框选聊天气泡滚动区，回车确认，Esc取消"
            << "platformId=" << platformId
            << "handle=0x" << QString::number(static_cast<qulonglong>(entry.handle), 16)
            << "scale=" << scale
            << "targetRectPx(" << formatRect(targetRectPx) << ")"
            << "targetRectLogical(" << formatRect(QRect(QPoint(0, 0), logicalSize)) << ")";

    overlay->setOnFinished([this, platformId](bool ok, const QRect& selectionLogical) {
        if (!ok) {
            qInfo() << "[MainWindow] 微信RPA校准已取消";
            return;
        }
        if (!m_managedWindows.contains(platformId)) {
            qWarning() << "[MainWindow] 微信RPA校准失败：平台已移除";
            return;
        }

        const auto& entry2 = m_managedWindows[platformId];
        if (!Win32WindowHelper::isWindowValid(entry2.handle)) {
            qWarning() << "[MainWindow] 微信RPA校准失败：窗口无效";
            return;
        }

        // Convert logical selection (relative to m_centerStack) -> window-relative pixels
        QScreen* screen = m_centerStack->screen();
        if (!screen && windowHandle())
            screen = windowHandle()->screen();
        const qreal scale = screen ? screen->devicePixelRatio() : 1.0;

        const QRect chatRectInWindowPx(qRound(selectionLogical.x() * scale),
                                       qRound(selectionLogical.y() * scale),
                                       qRound(selectionLogical.width() * scale),
                                       qRound(selectionLogical.height() * scale));

        const QSize windowSizePx = entry2.lastDisplayGeometry.isValid()
                                       ? entry2.lastDisplayGeometry.size()
                                       : Win32WindowHelper::windowRect(entry2.handle).size();

        const bool saved = writeWechatRpaConfigRelativeToWindow(entry2.handle,
                                                                chatRectInWindowPx,
                                                                windowSizePx,
                                                                QStringLiteral("demo_wechat_conv_1"),
                                                                QStringLiteral("演示微信联系人"));

        qInfo() << "[MainWindow] 微信RPA校准结果:"
                << "saved=" << saved
                << "scale=" << scale
                << "windowSizePx=" << windowSizePx
                << "chatRectInWindowPx(" << formatRect(chatRectInWindowPx) << ")";

        statusBar()->showMessage(saved
                                     ? QStringLiteral("微信RPA校准已保存（python/rpa/wechat_config.json）")
                                     : QStringLiteral("微信RPA校准保存失败，请查看日志"),
                                 4000);
    });
}

bool MainWindow::writeWechatRpaConfigRelativeToWindow(quintptr hwnd,
                                                      const QRect& chatRectInWindowPx,
                                                      const QSize& windowSizePx,
                                                      const QString& platformConversationId,
                                                      const QString& customerName) const
{
    const QString path = QStringLiteral(PROJECT_ROOT_DIR) + QStringLiteral("/python/rpa/wechat_config.json");

    QJsonObject root;
    root.insert(QStringLiteral("platform"), QStringLiteral("wechat_pc"));
    root.insert(QStringLiteral("poll_interval_sec"), 3);

    QJsonObject windowMatch;
    // Some distributions use Weixin.exe (common on CN Windows). Use that as default.
    windowMatch.insert(QStringLiteral("process_name"), QStringLiteral("Weixin.exe"));
    windowMatch.insert(QStringLiteral("title_contains"), QStringLiteral("微信"));
    root.insert(QStringLiteral("window_match"), windowMatch);

    // Persist hwnd from the currently managed window if possible.
    // Python reader can use it directly to avoid EnumWindows ambiguity under Embed/FloatFollow.
    root.insert(QStringLiteral("hwnd_hex"), QStringLiteral("0x%1").arg(QString::number(static_cast<qulonglong>(hwnd), 16)));

    QJsonObject chatRegion;
    chatRegion.insert(QStringLiteral("mode"), QStringLiteral("relative_to_window"));
    chatRegion.insert(QStringLiteral("x"), chatRectInWindowPx.x());
    chatRegion.insert(QStringLiteral("y"), chatRectInWindowPx.y());
    chatRegion.insert(QStringLiteral("w"), chatRectInWindowPx.width());
    chatRegion.insert(QStringLiteral("h"), chatRectInWindowPx.height());
    root.insert(QStringLiteral("chat_region"), chatRegion);

    QJsonObject windowSize;
    windowSize.insert(QStringLiteral("w"), windowSizePx.width());
    windowSize.insert(QStringLiteral("h"), windowSizePx.height());
    root.insert(QStringLiteral("window_size_px"), windowSize);

    QJsonObject conv;
    conv.insert(QStringLiteral("platform_conversation_id"), platformConversationId);
    conv.insert(QStringLiteral("customer_name"), customerName);
    root.insert(QStringLiteral("conversation"), conv);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "[MainWindow] 写入 wechat_config.json 失败:" << path << f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

// ==================== Aggregate Chat ====================

void MainWindow::openAggregateChatForm()
{
    if (!m_aggregateChatForm) {
        m_aggregateChatForm = new AggregateChatForm(this);
        m_centerStack->addWidget(m_aggregateChatForm);
    }
    m_centerStack->setCurrentWidget(m_aggregateChatForm);
}

// ==================== EmbeddedWindowContainer ====================

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
        Win32WindowHelper::resizeEmbeddedWindow(m_handle, this);
    }
}

quintptr EmbeddedWindowContainer::embeddedHandle() const
{
    return m_handle;
}

void EmbeddedWindowContainer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_handle) {
        Win32WindowHelper::resizeEmbeddedWindow(m_handle, this);
    }
}

// ==================== Window Events ====================

void MainWindow::closeEvent(QCloseEvent* event)
{
    detachAllWindows();
    QMainWindow::closeEvent(event);
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    updateFloatFollowPosition();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateFloatFollowPosition();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        if (isMinimized()) {
            hideCurrentFloatWindow();
        } else {
            QTimer::singleShot(100, this, &MainWindow::updateFloatFollowPosition);
        }
    } else if (event->type() == QEvent::ActivationChange) {
        if (isActiveWindow()) {
            QTimer::singleShot(50, this, &MainWindow::updateFloatFollowPosition);
        }
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_statusMessage) {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                refreshStatusMessage();
                return true;
            }
        } else if (event->type() == QEvent::ContextMenu) {
            auto* contextEvent = static_cast<QContextMenuEvent*>(event);
            QMenu menu(this);
            QAction* actManage = menu.addAction(QStringLiteral("管理文案"));
            QAction* chosen = menu.exec(contextEvent->globalPos());
            if (chosen == actManage) {
                openStatusMessageManager();
            }
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshStatusMessage()
{
    if (!m_statusMessage)
        return;

    const QStringList messages = allEncouragementMessages(m_customStatusMessages);
    m_statusMessage->setText(randomEncouragementText(messages, m_statusMessage->text()));
}

void MainWindow::openStatusMessageManager()
{
    const ApplyStyle::MainWindowTheme theme = m_mainWindowTheme;
    QDialog dialog(this, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dialog.setObjectName(QStringLiteral("statusMessageManagerDialog"));
    dialog.setWindowTitle(QStringLiteral("管理文案"));
    dialog.setModal(true);
    dialog.resize(450, 320);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* tipLabel = new QLabel(QStringLiteral("这里管理自定义文案；内置文案仍会默认参与随机显示。"), &dialog);
    tipLabel->setWordWrap(true);

    auto* listWidget = new QListWidget(&dialog);
    listWidget->setObjectName(QStringLiteral("statusMessageList"));
    listWidget->addItems(m_customStatusMessages);

    auto* editor = new QLineEdit(&dialog);
    editor->setObjectName(QStringLiteral("statusMessageEditor"));
    editor->setPlaceholderText(QStringLiteral("输入一句想展示的话"));

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    auto* addButton = new QPushButton(QStringLiteral("新增"), &dialog);
    auto* updateButton = new QPushButton(QStringLiteral("修改"), &dialog);
    auto* deleteButton = new QPushButton(QStringLiteral("删除"), &dialog);
    auto* closeButton = new QPushButton(QStringLiteral("关闭"), &dialog);
    addButton->setObjectName(QStringLiteral("statusMessageAddButton"));
    updateButton->setObjectName(QStringLiteral("statusMessageUpdateButton"));
    deleteButton->setObjectName(QStringLiteral("statusMessageDeleteButton"));
    closeButton->setObjectName(QStringLiteral("statusMessageCloseButton"));
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(updateButton);
    buttonRow->addWidget(deleteButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButton);

    layout->addWidget(tipLabel);
    layout->addWidget(listWidget, 1);
    layout->addWidget(editor);
    layout->addLayout(buttonRow);

    auto syncCustomMessages = [this, listWidget]() {
        QStringList messages;
        for (int i = 0; i < listWidget->count(); ++i)
            messages.append(listWidget->item(i)->text());
        m_customStatusMessages = normalizedMessages(messages);
        saveCustomEncouragementMessages(m_customStatusMessages);
        refreshStatusMessage();
    };

    auto findRowByText = [listWidget](const QString& text, int ignoreRow = -1) {
        for (int i = 0; i < listWidget->count(); ++i) {
            if (i != ignoreRow && listWidget->item(i)->text() == text)
                return i;
        }
        return -1;
    };

    connect(listWidget, &QListWidget::currentTextChanged, &dialog, [editor](const QString& text) {
        editor->setText(text);
        editor->selectAll();
    });
    connect(addButton, &QPushButton::clicked, &dialog, [=]() {
        const QString text = editor->text().trimmed();
        if (text.isEmpty()) {
            editor->setFocus();
            return;
        }

        const int existingRow = findRowByText(text);
        if (existingRow >= 0) {
            listWidget->setCurrentRow(existingRow);
            editor->setFocus();
            return;
        }

        listWidget->addItem(text);
        listWidget->setCurrentRow(listWidget->count() - 1);
        syncCustomMessages();
        editor->clear();
        editor->setFocus();
    });
    connect(updateButton, &QPushButton::clicked, &dialog, [=]() {
        auto* currentItem = listWidget->currentItem();
        if (!currentItem) {
            editor->setFocus();
            return;
        }

        const QString text = editor->text().trimmed();
        if (text.isEmpty()) {
            editor->setFocus();
            return;
        }

        const int currentRow = listWidget->row(currentItem);
        const int existingRow = findRowByText(text, currentRow);
        if (existingRow >= 0) {
            listWidget->setCurrentRow(existingRow);
            editor->setFocus();
            return;
        }

        currentItem->setText(text);
        syncCustomMessages();
    });
    connect(deleteButton, &QPushButton::clicked, &dialog, [=]() {
        auto* currentItem = listWidget->currentItem();
        if (!currentItem)
            return;

        delete listWidget->takeItem(listWidget->row(currentItem));
        syncCustomMessages();
        editor->clear();
        editor->setFocus();
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(editor, &QLineEdit::returnPressed, &dialog, [=]() {
        if (listWidget->currentItem())
            updateButton->click();
        else
            addButton->click();
    });

    dialog.setStyleSheet(ApplyStyle::statusMessageManagerStyle(theme));
    dialog.adjustSize();

    if (m_statusMessage) {
        const QSize dialogSize = dialog.size();
        QPoint anchor = m_statusMessage->mapToGlobal(QPoint(m_statusMessage->width(), 0));
        QScreen* screen = QGuiApplication::screenAt(anchor);
        if (!screen && windowHandle())
            screen = windowHandle()->screen();
        if (!screen)
            screen = QGuiApplication::primaryScreen();

        QRect available = screen ? screen->availableGeometry() : QRect();
        int x = anchor.x() - dialogSize.width();
        int y = anchor.y() - dialogSize.height() - 8;
        if (screen) {
            x = qBound(available.left(), x, available.right() - dialogSize.width());
            if (y < available.top())
                y = qMin(anchor.y() + m_statusMessage->height() + 8, available.bottom() - dialogSize.height());
        }
        dialog.move(x, y);
    }

    dialog.exec();
}

void MainWindow::buildStatusBar()
{
    m_customStatusMessages = loadCustomEncouragementMessages();
    m_btnThemeSwitch = new QToolButton(this);
    m_btnThemeSwitch->setObjectName(QStringLiteral("themeSwitchButton"));
    m_btnThemeSwitch->setText(QStringLiteral("主题"));
    m_btnThemeSwitch->setCursor(Qt::PointingHandCursor);
    m_btnThemeSwitch->setPopupMode(QToolButton::InstantPopup);
    m_btnThemeSwitch->setToolTip(QStringLiteral("默认 / 冷色 / 暖色"));
    auto* themeMenu = new QMenu(m_btnThemeSwitch);
    themeMenu->addAction(QStringLiteral("默认"), this, [this]() {
        applyMainWindowTheme(ApplyStyle::MainWindowTheme::Default);
    });
    themeMenu->addAction(QStringLiteral("冷色"), this, [this]() {
        applyMainWindowTheme(ApplyStyle::MainWindowTheme::Cool);
    });
    themeMenu->addAction(QStringLiteral("暖色"), this, [this]() {
        applyMainWindowTheme(ApplyStyle::MainWindowTheme::Warm);
    });
    m_btnThemeSwitch->setMenu(themeMenu);
    statusBar()->addWidget(m_btnThemeSwitch);

    auto* statusWrap = new QWidget(this);
    statusWrap->setObjectName("statusBarWrap");
    auto* statusLayout = new QHBoxLayout(statusWrap);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(0);

    m_statusMessage = new QLabel(statusWrap);
    m_statusSeparator = new QLabel(statusWrap);
    m_statusTime = new QLabel(statusWrap);
    m_statusMessage->setObjectName("statusMessage");
    m_statusSeparator->setObjectName("statusSeparator");
    m_statusTime->setObjectName("statusTime");
    m_statusMessage->setCursor(Qt::PointingHandCursor);
    m_statusMessage->setToolTip(QStringLiteral("左键换一句，右键管理文案"));
    m_statusSeparator->setText(QStringLiteral(" | "));
    m_statusMessage->installEventFilter(this);
    statusLayout->addWidget(m_statusMessage);
    statusLayout->addWidget(m_statusSeparator);
    statusLayout->addWidget(m_statusTime);
    statusBar()->addPermanentWidget(statusWrap);
    auto* timeTimer = new QTimer(this);
    connect(timeTimer, &QTimer::timeout, this, [this]() {
        m_statusTime->setText(QDateTime::currentDateTime().toString(
            QStringLiteral("yyyy年MM月dd日 hh:mm:ss")));
    });
    timeTimer->start(1000);
    refreshStatusMessage();
    m_statusTime->setText(QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy年MM月dd日 hh:mm:ss")));
}
