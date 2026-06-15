#include "applystyle.h"

namespace {

/** 主窗口配色 Token（集中在此，MainWindow 不持有色值） */
struct MainWindowStyleTokens {
    const char* rootBg;
    const char* rightAreaBg;
    const char* leftSidebarBg;
    const char* topBarBg;
    const char* topBarBorderBottom;
    const char* topTitleColor;
    const char* readyWrapBg;
    const char* readyWrapBorder;
    const char* readyTextColor;
    const char* centerBg;
    const char* cardBg;
    const char* cardBorder;
    const char* rocketWrapBg;
    const char* readyTitleColor;
    const char* dividerBg;
    const char* mutedTextColor;
    const char* quickCardBg;
    const char* quickCardBorder;
    const char* quickCardText;
    const char* quickCardHoverBg;
    const char* quickCardHoverBorder;
    const char* quickCardPressedBg;
    const char* statusBarBg;
    const char* statusBarText;
    const char* statusBarBorderTop;
    const char* statusLabelColor;
    const char* themeButtonColor;
    const char* themeButtonBorder;
    const char* themeButtonHoverBg;
    /** 左侧平台树容器：与侧栏底色区分，形成「外深内浅」 */
    const char* platformTreePanelBg;
    const char* platformTreePanelBorder;
};

/*
 * Default = 轻量工作台：柔和底色承载页面层级，白色卡片表达可操作内容。
 */
const MainWindowStyleTokens kTokensDefault = {
    "#F3F6FB", /* rootBg */
    "#F3F6FB", /* rightAreaBg */
    "#FFFFFF", /* leftSidebarBg */
    "#FFFFFF", /* topBarBg */
    "#E6EDF7", /* topBarBorderBottom */
    "#0F172A", /* topTitleColor */
    "#ECFDF5", /* readyWrapBg */
    "#BBF7D0", /* readyWrapBorder */
    "#047857", /* readyTextColor */
    "#F3F6FB", /* centerBg */
    "#FFFFFF", /* cardBg */
    "#E5EDF7", /* cardBorder */
    "#EFF6FF", /* rocketWrapBg */
    "#0F172A", /* readyTitleColor */
    "#D7E3F4", /* dividerBg */
    "#64748B", /* mutedTextColor */
    "#FFFFFF", /* quickCardBg */
    "#E2E8F0", /* quickCardBorder */
    "#0F172A", /* quickCardText */
    "#F8FAFF", /* quickCardHoverBg */
    "#93C5FD", /* quickCardHoverBorder */
    "#EAF2FF", /* quickCardPressedBg */
    "#F8FAFC", /* statusBarBg */
    "#475569", /* statusBarText */
    "#E2E8F0", /* statusBarBorderTop */
    "#64748B", /* statusLabelColor */
    "#0F172A", /* themeButtonColor */
    "#CBD5E1", /* themeButtonBorder */
    "#EAF2FF", /* themeButtonHoverBg */
    "#F8FAFC", /* platformTreePanelBg */
    "#E2E8F0", /* platformTreePanelBorder */
};

/* 冷色：蓝灰、镇静、信任感，少色相 */
const MainWindowStyleTokens kTokensCool = {
    "#1e293b",
    "#1e293b",
    "#0f172a",
    "#f1f5f9",
    "#cbd5e1",
    "#0f172a",
    "#ffffff",
    "#e2e8f0",
    "#1e293b",
    "#e2e8f0",
    "#ffffff",
    "#cbd5e1",
    "#e0f2fe",
    "#1e293b",
    "#cbd5e1",
    "#64748b",
    "#ffffff",
    "#cbd5e1",
    "#334155",
    "#f8fafc",
    "#e2e8f0",
    "#f1f5f9",
    "#0f172a",
    "#e2e8f0",
    "#1e293b",
    "#e2e8f0",
    "#e2e8f0",
    "#334155",
    "#1e293b",
    "#1e293b",
    "#334155",
};

/* 暖色：米杏、安稳，点缀克制 */
const MainWindowStyleTokens kTokensWarm = {
    "#3d3835",
    "#3d3835",
    "#2a2624",
    "#faf7f2",
    "#e5ddd4",
    "#3d3429",
    "#fffcf9",
    "#ebe4db",
    "#3d3429",
    "#f5efe6",
    "#fffcf9",
    "#ebe4db",
    "#f7eede",
    "#3d3429",
    "#e5ddd4",
    "#7a7268",
    "#fffcf9",
    "#ebe4db",
    "#4a453d",
    "#faf5ed",
    "#e0d8ce",
    "#f0e8dc",
    "#3a3632",
    "#f2ebe3",
    "#4a4540",
    "#f2ebe3",
    "#f2ebe3",
    "#6b6560",
    "#4a4540",
    "#3d3835",
    "#7a5b43",
};

QString buildMainWindowStyleQss(const MainWindowStyleTokens& t)
{
    return QStringLiteral(
        R"QSS(
        QWidget#root { background: %1; }

        QWidget#rightArea { background: %2; }
        QWidget#leftSidebar {
            background: %3;
            border-right: 1px solid %5;
        }
        QTreeView#platformList {
            background: transparent;
            border: none;
            border-radius: 0;
            outline: none;
        }
        QTreeView#platformList::item { background: transparent; }

        QWidget#userProfileBar {
            border-radius: 14px;
            background: transparent;
        }
        QWidget#userProfileBar:hover {
            background: %29;
        }
        QLabel#userProfileNick {
            color: %27;
            font-size: 13px;
            font-weight: 600;
            background: transparent;
        }
        QLabel#sidebarAvatar {
            background: transparent;
            border-radius: 10px;
        }

        QWidget#topBar {
            background: %4;
            border-bottom: 1px solid %5;
        }
        QLabel#topTitle {
            color: %6;
            font-size: 15px;
            font-weight: 700;
            background: transparent;
        }
        QWidget#readyWrap {
            background: %7;
            border: 1px solid %8;
            border-radius: 14px;
        }
        QLabel#readyText {
            color: %9;
            font-size: 12px;
            font-weight: 600;
            background: transparent;
        }
        QToolButton#topIconButton {
            background: transparent;
            border: none;
            border-radius: 12px;
            padding: 6px;
            min-width: 36px;
            min-height: 36px;
        }
        QToolButton#topIconButton:hover {
            background: %29;
        }
        QToolButton#topIconButton:pressed {
            background: %22;
        }

        QWidget#centerArea, QWidget#centerStack, QWidget#placeholderPage {
            background: %10;
        }

        QFrame#readyCard, QFrame#platformCard {
            background: %11;
            border-radius: 22px;
            border: 1px solid %12;
        }
        QFrame#rocketWrap {
            background: %13;
            border: 1px solid %12;
            border-radius: 18px;
        }

        QLabel#readyTitle {
            color: %14;
            font-size: 22px;
            font-weight: 700;
            background: transparent;
        }
        QFrame#divider {
            background: %15;
        }
        QLabel#readySubtitle {
            color: %16;
            font-size: 13px;
            background: transparent;
        }
        QLabel#placeholderText {
            color: %16;
            font-size: 14px;
        }

        QToolButton#quickCard {
            background: %17;
            border: 1px solid %18;
            border-radius: 18px;
            padding: 12px 10px;
            color: %19;
            font-size: 13px;
            font-weight: 650;
        }
        QToolButton#quickCard:hover {
            background: %20;
            border-color: %21;
        }
        QToolButton#quickCard:pressed {
            background: %22;
        }
        QToolButton#quickCard:disabled {
            background: %17;
            border-color: %18;
            color: %16;
        }

        QStatusBar {
            background: %23;
            color: %24;
            font-size: 12px;
            border-top: 1px solid %25;
            min-height: 24px;
        }
        QStatusBar::item { border: none; }
        QWidget#statusBarWrap {
            background: transparent;
        }
        QLabel#statusMessage,
        QLabel#statusSeparator,
        QLabel#statusTime {
            color: %26;
            background: transparent;
        }
        QLabel#statusMessage:hover {
            color: %6;
        }
        QToolButton#themeSwitchButton {
            color: %27;
            background: transparent;
            border: 1px solid %28;
            border-radius: 4px;
            padding: 2px 10px;
            font-size: 12px;
        }
        QToolButton#themeSwitchButton:hover {
            background: %29;
        }
        QToolButton#themeSwitchButton::menu-indicator {
            image: none;
            width: 0;
        }
    )QSS")
        .arg(QLatin1String(t.rootBg))
        .arg(QLatin1String(t.rightAreaBg))
        .arg(QLatin1String(t.leftSidebarBg))
        .arg(QLatin1String(t.topBarBg))
        .arg(QLatin1String(t.topBarBorderBottom))
        .arg(QLatin1String(t.topTitleColor))
        .arg(QLatin1String(t.readyWrapBg))
        .arg(QLatin1String(t.readyWrapBorder))
        .arg(QLatin1String(t.readyTextColor))
        .arg(QLatin1String(t.centerBg))
        .arg(QLatin1String(t.cardBg))
        .arg(QLatin1String(t.cardBorder))
        .arg(QLatin1String(t.rocketWrapBg))
        .arg(QLatin1String(t.readyTitleColor))
        .arg(QLatin1String(t.dividerBg))
        .arg(QLatin1String(t.mutedTextColor))
        .arg(QLatin1String(t.quickCardBg))
        .arg(QLatin1String(t.quickCardBorder))
        .arg(QLatin1String(t.quickCardText))
        .arg(QLatin1String(t.quickCardHoverBg))
        .arg(QLatin1String(t.quickCardHoverBorder))
        .arg(QLatin1String(t.quickCardPressedBg))
        .arg(QLatin1String(t.statusBarBg))
        .arg(QLatin1String(t.statusBarText))
        .arg(QLatin1String(t.statusBarBorderTop))
        .arg(QLatin1String(t.statusLabelColor))
        .arg(QLatin1String(t.themeButtonColor))
        .arg(QLatin1String(t.themeButtonBorder))
        .arg(QLatin1String(t.themeButtonHoverBg))
        .arg(QLatin1String(t.platformTreePanelBg))
        .arg(QLatin1String(t.platformTreePanelBorder));
}

QString baseControlStyleQss()
{
    return QStringLiteral(
        R"QSS(
        QPushButton {
            background: #FFFFFF;
            border: 1px solid #E4E4E7;
            border-radius: 8px;
            color: #18181B;
            font-size: 13px;
            padding: 6px 14px;
            min-height: 22px;
        }
        QPushButton:hover {
            background: #F5F5F7;
            border-color: #D4D4D8;
        }
        QPushButton:pressed {
            background: #EAF2FF;
            border-color: #2563EB;
        }
        QPushButton:focus {
            border-color: #2563EB;
        }
        QPushButton:disabled {
            background: #FAFAFB;
            border-color: #E4E4E7;
            color: #A1A1AA;
        }
        QPushButton:default {
            background: #2563EB;
            border-color: #2563EB;
            color: #FFFFFF;
            font-weight: 600;
        }
        QPushButton:default:hover {
            background: #1D4ED8;
            border-color: #1D4ED8;
        }
        QPushButton:default:pressed {
            background: #1E40AF;
            border-color: #1E40AF;
        }

        QLineEdit,
        QPlainTextEdit,
        QTextEdit,
        QComboBox,
        QSpinBox,
        QDoubleSpinBox {
            background: #FFFFFF;
            border: 1px solid #E4E4E7;
            border-radius: 8px;
            color: #18181B;
            font-size: 13px;
            selection-background-color: #EAF2FF;
            selection-color: #18181B;
        }
        QLineEdit,
        QComboBox,
        QSpinBox,
        QDoubleSpinBox {
            min-height: 28px;
            padding: 4px 10px;
        }
        QPlainTextEdit,
        QTextEdit {
            padding: 8px 10px;
        }
        QLineEdit:hover,
        QPlainTextEdit:hover,
        QTextEdit:hover,
        QComboBox:hover,
        QSpinBox:hover,
        QDoubleSpinBox:hover {
            border-color: #D4D4D8;
        }
        QLineEdit:focus,
        QPlainTextEdit:focus,
        QTextEdit:focus,
        QComboBox:focus,
        QSpinBox:focus,
        QDoubleSpinBox:focus {
            border-color: #2563EB;
        }
        QLineEdit:disabled,
        QPlainTextEdit:disabled,
        QTextEdit:disabled,
        QComboBox:disabled,
        QSpinBox:disabled,
        QDoubleSpinBox:disabled {
            background: #FAFAFB;
            border-color: #E4E4E7;
            color: #A1A1AA;
        }

        QListWidget,
        QTreeWidget,
        QTreeView,
        QTableWidget,
        QTableView {
            background: #FFFFFF;
            alternate-background-color: #FAFAFB;
            border: 1px solid #E4E4E7;
            border-radius: 8px;
            color: #18181B;
            outline: none;
            selection-background-color: #EAF2FF;
            selection-color: #18181B;
        }
        QListWidget::item,
        QTreeWidget::item,
        QTreeView::item,
        QTableWidget::item,
        QTableView::item {
            color: #18181B;
        }
        QListWidget::item:hover,
        QTreeWidget::item:hover,
        QTreeView::item:hover,
        QTableWidget::item:hover,
        QTableView::item:hover {
            background: #F0F2F5;
        }
        QListWidget::item:selected,
        QTreeWidget::item:selected,
        QTreeView::item:selected,
        QTableWidget::item:selected,
        QTableView::item:selected {
            background: #EAF2FF;
            color: #18181B;
        }
        QHeaderView::section {
            background: #FAFAFB;
            border: none;
            border-bottom: 1px solid #E4E4E7;
            border-right: 1px solid #E4E4E7;
            color: #71717A;
            font-size: 12px;
            font-weight: 600;
            padding: 7px 10px;
        }
    )QSS");
}

} // namespace

/** 全局悬浮滚动条：透明轨道、无箭头、胶囊滑块；显隐与淡出由 ScrollBehavior 管理。 */
static QString unifiedScrollBarQss(ApplyStyle::MainWindowTheme theme)
{
    Q_UNUSED(theme)
    return QStringLiteral(
        R"(
        QScrollBar:vertical {
            width: 10px;
            background: transparent;
            border: none;
            margin: 2px 2px 2px 2px;
        }
        QScrollBar::handle:vertical {
            background: rgba(113, 113, 122, 150);
            min-height: 36px;
            border-radius: 3px;
        }
        QScrollBar::handle:vertical:hover {
            background: rgba(82, 82, 91, 200);
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
            width: 0;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollBar:horizontal {
            height: 10px;
            background: transparent;
            border: none;
            margin: 2px 2px 2px 2px;
        }
        QScrollBar::handle:horizontal {
            background: rgba(113, 113, 122, 150);
            min-width: 36px;
            border-radius: 3px;
        }
        QScrollBar::handle:horizontal:hover {
            background: rgba(82, 82, 91, 200);
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            height: 0;
            width: 0;
        }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: transparent;
        }
        QScrollBar::handle:vertical:disabled,
        QScrollBar::handle:horizontal:disabled {
            background: transparent;
        }
    )");
}

/** 「添加新窗口 / Python 服务端连接」等 QDialog 内 QComboBox，与聚合接待模式下拉一致 */
static QString addWindowDialogComboBoxQss(ApplyStyle::MainWindowTheme theme)
{
    Q_UNUSED(theme)
    return QStringLiteral(
        R"QSS(
        QDialog QComboBox {
            color: #18181B;
            background-color: #FFFFFF;
            border: 1px solid #E4E4E7;
            border-radius: 8px;
            padding: 4px 10px;
            min-height: 24px;
            font-size: 12px;
        }
        QDialog QComboBox:hover {
            border-color: #D4D4D8;
        }
        QDialog QComboBox:focus {
            border-color: #2563EB;
        }
        QDialog QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 26px;
            border: none;
            border-left: 1px solid #E4E4E7;
            border-top-right-radius: 7px;
            border-bottom-right-radius: 7px;
            background: transparent;
        }
        QDialog QComboBox::down-arrow {
            image: url(:/fold_arrow_to_expand_icon.svg);
            width: 16px;
            height: 16px;
        }
        QDialog QComboBox[popupOpen="true"]::down-arrow {
            image: url(:/fold_arrow_to_collapse_icon.svg);
        }
        QDialog QComboBox QAbstractItemView {
            background: #FFFFFF;
            color: #18181B;
            selection-background-color: #EAF2FF;
            selection-color: #18181B;
            border: 1px solid #E4E4E7;
            outline: none;
            padding: 2px;
        }
    )QSS");
}

PlatformTreeColors ApplyStyle::platformTreeColors(MainWindowTheme theme)
{
    theme = MainWindowTheme::Default;
    switch (theme) {
    case MainWindowTheme::Cool: {
        PlatformTreeColors c;
        c.groupBgDefault = QColor(0x1e, 0x29, 0x3b);
        c.groupBgHover = QColor(0x33, 0x41, 0x55);
        c.groupBgSelected = QColor(0x1e, 0x3a, 0x5f);
        c.groupTextColor = QColor(0xe2, 0xe8, 0xf0);
        c.groupArrowColor = QColor(0x94, 0xa3, 0xb8);
        c.itemBgDefault = QColor(0x1e, 0x29, 0x3b);
        c.itemBgHover = QColor(0x33, 0x41, 0x55);
        c.itemBgSelected = QColor(0x1e, 0x3a, 0x5f);
        c.itemTextColor = QColor(0xe2, 0xe8, 0xf0);
        c.itemAccentBarColor = QColor(0x38, 0xbd, 0xf8);
        c.itemInactiveBgDefault = QColor(0x0f, 0x17, 0x2a);
        c.itemInactiveBgHover = QColor(0x1e, 0x29, 0x3b);
        c.itemInactiveBgSelected = QColor(0x1e, 0x3a, 0x5f);
        c.itemInactiveTextColor = QColor(0x94, 0xa3, 0xb8);
        c.csDotActivated = QColor(0x4a, 0xe0, 0x82);
        c.csDotInactive = QColor(0x64, 0x74, 0x8b);
        c.itemInactiveIconOpacity = 0.35;
        return c;
    }
    case MainWindowTheme::Warm: {
        PlatformTreeColors c;
        c.groupBgDefault = QColor(0x2a, 0x26, 0x24);
        c.groupBgHover = QColor(0x3d, 0x38, 0x35);
        c.groupBgSelected = QColor(0x45, 0x3a, 0x2a);
        c.groupTextColor = QColor(0xf5, 0xef, 0xe6);
        c.groupArrowColor = QColor(0xa8, 0x92, 0x7a);
        c.itemBgDefault = QColor(0x3d, 0x38, 0x35);
        c.itemBgHover = QColor(0x4a, 0x45, 0x40);
        c.itemBgSelected = QColor(0x45, 0x3a, 0x2a);
        c.itemTextColor = QColor(0xf5, 0xef, 0xe6);
        c.itemAccentBarColor = QColor(0xf5, 0x9e, 0x0b);
        c.itemInactiveBgDefault = QColor(0x2a, 0x26, 0x24);
        c.itemInactiveBgHover = QColor(0x3d, 0x38, 0x35);
        c.itemInactiveBgSelected = QColor(0x45, 0x3a, 0x2a);
        c.itemInactiveTextColor = QColor(0xa8, 0x92, 0x7a);
        c.csDotActivated = QColor(0x84, 0xcc, 0x16);
        c.csDotInactive = QColor(0x78, 0x72, 0x68);
        c.itemInactiveIconOpacity = 0.35;
        return c;
    }
    case MainWindowTheme::Default:
    default: {
        PlatformTreeColors c;
        /* Light rail: keep the surface quiet, but make hover/selection easier to read. */
        c.groupBgDefault = QColor(0, 0, 0, 0);
        c.groupBgHover = QColor(0xef, 0xf6, 0xff);
        c.groupBgSelected = QColor(0xdb, 0xea, 0xfe);
        c.groupTextColor = QColor(0x18, 0x18, 0x1b);
        c.groupArrowColor = QColor(0x60, 0x73, 0x8b);
        c.itemBgDefault = QColor(0, 0, 0, 0);
        c.itemBgHover = QColor(0xef, 0xf6, 0xff);
        c.itemBgSelected = QColor(0xdb, 0xea, 0xfe);
        c.itemTextColor = QColor(0x0f, 0x17, 0x2a);
        c.itemAccentBarColor = QColor(0x25, 0x63, 0xeb);
        c.itemInactiveBgDefault = QColor(0, 0, 0, 0);
        c.itemInactiveBgHover = QColor(0xf8, 0xfa, 0xfc);
        c.itemInactiveBgSelected = QColor(0xdb, 0xea, 0xfe);
        c.itemInactiveTextColor = QColor(0x64, 0x74, 0x8b);
        c.csDotActivated = QColor(0x16, 0xa3, 0x4a);
        c.csDotInactive = QColor(0xa1, 0xa1, 0xaa);
        c.itemInactiveIconOpacity = 0.5;
        return c;
    }
    }
}

QString ApplyStyle::loginWindowStyle()
{
    return QStringLiteral(R"QSS(
        LoginWindow {
            background: transparent;
        }
        #loginTitleBar {
            background-color: rgba(255, 255, 255, 0.82);
            border-bottom: 1px solid rgba(0, 0, 0, 0.06);
        }
        #loginTitleBarLabel {
            font-size: 13px;
            font-weight: 600;
            color: #1f1f1f;
            font-family: "Microsoft YaHei", "PingFang SC", sans-serif;
            padding-left: 12px;
        }
        #loginCloseBtn {
            border: none;
            border-top-right-radius: 14px;
            border-bottom-left-radius: 4px;
            min-width: 48px;
            max-width: 48px;
            min-height: 40px;
            max-height: 40px;
            padding: 0px;
            margin: 0px;
            background: transparent;
        }
        #loginCloseBtn:hover {
            background-color: #e81123;
        }
        #loginAvatar {
            border-radius: 16px;
            background-color: transparent;
        }
        #loginCard {
            background-color: #FFFFFF;
            border-radius: 12px;
            border: 1px solid rgba(0,0,0,0.06);
        }
        #loginTitle {
            font-size: 20px;
            font-weight: bold;
            color: #262626;
            font-family: "Microsoft YaHei", "PingFang SC", sans-serif;
        }
        #loginSubtitle {
            font-size: 14px;
            color: #8C8C8C;
            font-family: "Microsoft YaHei", "PingFang SC", sans-serif;
        }
        #loginInput {
            font-size: 14px;
            padding: 8px 16px;
            border: 1px solid #E8E8E8;
            border-radius: 8px;
            background-color: #ede5e5;
            color: black;
        }
        #loginInput:focus {
            border-color: #1890FF;
        }
        #primaryButton {
            font-size: 14px;
            font-weight: bold;
            color: white;
            background-color: #1890FF;
            border: none;
            border-radius: 8px;
        }
        #primaryButton:hover {
            background-color: #40a9ff;
        }
        #primaryButton:pressed {
            background-color: #096dd9;
        }
        #textButton {
            font-size: 12px;
            color: #1890FF;
            background: transparent;
            border: none;
        }
        #textButton:hover {
            color: #40a9ff;
        }
        #errorLabel {
            font-size: 12px;
            color: #ff4d4f;
        }
        #passwordVisibleCheck {
            font-size: 12px;
            color: #8C8C8C;
        }
    )QSS");
}

QString ApplyStyle::messageBoxContrastStyle()
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

QString ApplyStyle::globalScrollBarStyle()
{
    return unifiedScrollBarQss(MainWindowTheme::Default);
}

QString ApplyStyle::mainWindowStyle(MainWindowTheme theme)
{
    Q_UNUSED(theme)
    return buildMainWindowStyleQss(kTokensDefault)
        + baseControlStyleQss()
        + unifiedScrollBarQss(MainWindowTheme::Default);
}

QString ApplyStyle::mainWindowStyle()
{
    return mainWindowStyle(MainWindowTheme::Default);
}

ApplyStyle::MainWindowTheme ApplyStyle::loadSavedMainWindowTheme()
{
    return MainWindowTheme::Default;
}

void ApplyStyle::saveMainWindowTheme(MainWindowTheme theme)
{
    Q_UNUSED(theme)
}

/** 冷色主题下「添加新窗口」类对话框：#deebfb / #c0d9f7 系 */
static QString addWindowDialogStyleLegacyQss()
{
    return QStringLiteral(R"QSS(
        QDialog {
            background-color: #deebfb;
        }
        QDialog QLabel {
            color: #2d3748;
            background: transparent;
            font-size: 13px;
        }
        QDialog QLineEdit {
            color: #1e293b;
            background-color: #ffffff;
            border: 1px solid #c0d9f7;
            border-radius: 8px;
            padding: 6px 10px;
            font-size: 13px;
            selection-background-color: #c0d9f7;
            selection-color: #1e293b;
        }
        QDialog QLineEdit:focus {
            border-color: #8eb4e0;
        }
        QDialog QLineEdit:read-only {
            background-color: #deebfb;
            color: #334155;
        }
        QDialog QPushButton {
            color: #1e293b;
            background-color: #ffffff;
            border: 1px solid #c0d9f7;
            border-radius: 8px;
            padding: 6px 16px;
            font-size: 13px;
            min-height: 22px;
        }
        QDialog QPushButton:hover {
            background-color: #a8c9ef;
            border-color: #8eb4e0;
        }
        QDialog QPushButton:focus {
            background-color: #ffffff;
            border: 2px solid #93c5fd;
            color: #1e293b;
        }
        QDialog QPushButton:hover:focus {
            background-color: #e0f2fe;
            border-color: #60a5fa;
        }
        QDialog QPushButton:pressed {
            background-color: #8eb4e0;
            border-color: #7aa8d4;
        }
        QDialog QPushButton:pressed:focus {
            border-color: #7aa8d4;
        }
        QDialog QPushButton:default {
            color: #1e293b;
            background-color: #c0d9f7;
            border: 1px solid #a8c9ef;
            font-weight: 600;
        }
        QDialog QPushButton:default:hover {
            background-color: #a8c9ef;
            border-color: #8eb4e0;
            color: #1e293b;
        }
        QDialog QPushButton:default:focus {
            border: 2px solid #60a5fa;
            background-color: #c0d9f7;
            color: #1e293b;
        }
        QDialog QPushButton:default:hover:focus {
            background-color: #a8c9ef;
            border-color: #60a5fa;
            color: #1e293b;
        }
        QDialog QPushButton:default:pressed {
            background-color: #8eb4e0;
            border-color: #7aa8d4;
            color: #1e293b;
        }
        QDialog QPushButton:default:pressed:focus {
            border-color: #7aa8d4;
        }
        QDialog QTableWidget {
            background-color: #ffffff;
            alternate-background-color: #eef6fc;
            color: #1e293b;
            gridline-color: #c0d9f7;
            border: 1px solid #c0d9f7;
            border-radius: 10px;
            font-size: 13px;
            selection-background-color: #b8d4f5;
            selection-color: #1e293b;
        }
        QDialog QTableWidget:focus {
            outline: none;
        }
        QDialog QTableWidget::item {
            color: #1e293b;
            padding: 4px;
        }
        QDialog QTableWidget::item:hover {
            background-color: #e0f2fe;
            color: #1e293b;
        }
        QDialog QTableWidget::item:selected {
            background-color: #b8d4f5;
            color: #1e293b;
        }
        QDialog QTableWidget::item:selected:hover {
            background-color: #a8c9ef;
            color: #1e293b;
        }
        QDialog QTableWidget QTableCornerButton::section {
            background-color: #c0d9f7;
            border: 1px solid #a8c9ef;
        }
        QDialog QTableWidget QHeaderView {
            background-color: #c0d9f7;
        }
        QDialog QTableWidget QHeaderView::section:horizontal {
            background-color: #c0d9f7;
            color: #1e293b;
            padding: 8px;
            border: none;
            border-bottom: 1px solid #a8c9ef;
            border-right: 1px solid #a8c9ef;
            font-size: 12px;
            font-weight: 600;
        }
        QDialog QTableWidget QHeaderView::section:vertical {
            background-color: #deebfb;
            color: #1e293b;
            padding: 4px;
            border: none;
            border-right: 1px solid #c0d9f7;
            border-bottom: 1px solid #c0d9f7;
            font-size: 12px;
        }
        QDialog QCheckBox {
            color: #2d3748;
            spacing: 6px;
        }
        QDialog QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 3px;
        }
        QDialog QCheckBox::indicator:unchecked {
            background-color: #ffffff;
            border: 1px solid #c0d9f7;
        }
        QDialog QCheckBox::indicator:unchecked:hover {
            border-color: #8eb4e0;
        }
        QDialog QCheckBox::indicator:checked {
            image: url(:/add_window_checkbox_checked_cool.svg);
            border: none;
        }
        QDialog QCheckBox::indicator:checked:hover {
            image: url(:/add_window_checkbox_checked_cool_hover.svg);
        }
        QDialog QCheckBox::indicator:disabled {
            background-color: #eef6fc;
            border: 1px solid #c0d9f7;
        }
    )QSS");
}

/** 暖色主题：米杏底、琥珀强调，对齐主窗 kTokensWarm */
static QString addWindowDialogStyleWarmQss()
{
    return QStringLiteral(R"QSS(
        QDialog {
            background-color: #f5efe6;
        }
        QDialog QLabel {
            color: #3d3429;
            background: transparent;
            font-size: 13px;
        }
        QDialog QLineEdit {
            color: #3d3429;
            background-color: #fffcf9;
            border: 1px solid #e5ddd4;
            border-radius: 8px;
            padding: 6px 10px;
            font-size: 13px;
            selection-background-color: #fef3c7;
            selection-color: #3d3429;
        }
        QDialog QLineEdit:focus {
            border-color: #b45309;
        }
        QDialog QLineEdit:read-only {
            background-color: #ebe4db;
            color: #7a7268;
        }
        QDialog QPushButton {
            color: #3d3429;
            background-color: #fffcf9;
            border: 1px solid #e5ddd4;
            border-radius: 8px;
            padding: 6px 16px;
            font-size: 13px;
            min-height: 22px;
        }
        QDialog QPushButton:hover {
            background-color: #faf5ed;
            border-color: #d6cbc0;
        }
        QDialog QPushButton:focus {
            background-color: #faf5ed;
            border: 2px solid #e5ddd4;
            color: #3d3429;
        }
        QDialog QPushButton:hover:focus {
            background-color: #f5efe6;
            border-color: #d6cbc0;
        }
        QDialog QPushButton:pressed {
            background-color: #f0e8dc;
            border-color: #b45309;
        }
        QDialog QPushButton:pressed:focus {
            border-color: #b45309;
        }
        QDialog QPushButton:default {
            color: #fff7ed;
            background-color: #d97706;
            border: 1px solid #b45309;
            font-weight: 600;
        }
        QDialog QPushButton:default:hover {
            background-color: #ea580c;
            border-color: #c2410c;
            color: #fff7ed;
        }
        QDialog QPushButton:default:focus {
            border: 2px solid #fde68a;
            background-color: #d97706;
            color: #fff7ed;
        }
        QDialog QPushButton:default:hover:focus {
            background-color: #ea580c;
            border-color: #fbbf24;
            color: #fff7ed;
        }
        QDialog QPushButton:default:pressed {
            background-color: #b45309;
            border-color: #9a3412;
            color: #fff7ed;
        }
        QDialog QPushButton:default:pressed:focus {
            border-color: #9a3412;
        }
        QDialog QTableWidget {
            background-color: #fffcf9;
            alternate-background-color: #faf5ed;
            color: #3d3429;
            gridline-color: #e5ddd4;
            border: 1px solid #e5ddd4;
            border-radius: 10px;
            font-size: 13px;
            selection-background-color: #fef3c7;
            selection-color: #3d3429;
        }
        QDialog QTableWidget:focus {
            outline: none;
        }
        QDialog QTableWidget::item {
            color: #3d3429;
            padding: 4px;
        }
        QDialog QTableWidget::item:hover {
            background-color: #fff7ed;
            color: #3d3429;
        }
        QDialog QTableWidget::item:selected {
            background-color: #fef3c7;
            color: #3d3429;
        }
        QDialog QTableWidget::item:selected:hover {
            background-color: #fde68a;
            color: #3d3429;
        }
        QDialog QTableWidget QTableCornerButton::section {
            background-color: #ebe4db;
            border: 1px solid #d6cbc0;
        }
        QDialog QTableWidget QHeaderView {
            background-color: #ebe4db;
        }
        QDialog QTableWidget QHeaderView::section:horizontal {
            background-color: #ebe4db;
            color: #3d3429;
            padding: 8px;
            border: none;
            border-bottom: 1px solid #d6cbc0;
            border-right: 1px solid #d6cbc0;
            font-size: 12px;
            font-weight: 600;
        }
        QDialog QTableWidget QHeaderView::section:vertical {
            background-color: #f5efe6;
            color: #6b6560;
            padding: 4px;
            border: none;
            border-right: 1px solid #e5ddd4;
            border-bottom: 1px solid #e5ddd4;
            font-size: 12px;
        }
        QDialog QCheckBox {
            color: #3d3429;
            spacing: 6px;
        }
        QDialog QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 3px;
        }
        QDialog QCheckBox::indicator:unchecked {
            background-color: #fffcf9;
            border: 1px solid #e5ddd4;
        }
        QDialog QCheckBox::indicator:unchecked:hover {
            border-color: #d6cbc0;
        }
        QDialog QCheckBox::indicator:checked {
            image: url(:/add_window_checkbox_checked_warm.svg);
            border: none;
        }
        QDialog QCheckBox::indicator:checked:hover {
            image: url(:/add_window_checkbox_checked_warm_hover.svg);
        }
        QDialog QCheckBox::indicator:disabled {
            background-color: #faf5ed;
            border: 1px solid #e5ddd4;
        }
    )QSS");
}

/** Default：白底弹窗 + 浅蓝分割，与主窗浅蓝白一致 */
static QString addWindowDialogStyleSkyQss()
{
    return QStringLiteral(R"QSS(
        QDialog {
            background-color: #FFFFFF;
        }
        QDialog QLabel {
            color: #0F172A;
            background: transparent;
            font-size: 13px;
        }
        QDialog QLineEdit {
            color: #0F172A;
            background-color: #FFFFFF;
            border: 1px solid #BFDBFE;
            border-radius: 8px;
            padding: 6px 10px;
            font-size: 13px;
            selection-background-color: #E0F2FE;
            selection-color: #0F172A;
        }
        QDialog QLineEdit:focus {
            border-color: #00B2FF;
        }
        QDialog QLineEdit:read-only {
            background-color: #EFF6FF;
            color: #64748B;
        }
        QDialog QPushButton {
            color: #0F172A;
            background-color: #FFFFFF;
            border: 1px solid #BFDBFE;
            border-radius: 8px;
            padding: 6px 16px;
            font-size: 13px;
            min-height: 22px;
        }
        QDialog QPushButton:hover {
            background-color: #EFF6FF;
            border-color: #93C5FD;
        }
        QDialog QPushButton:focus {
            background-color: #EFF6FF;
            border: 2px solid #7DD3FC;
            color: #0F172A;
        }
        QDialog QPushButton:hover:focus {
            background-color: #E0F2FE;
            border-color: #7DD3FC;
        }
        QDialog QPushButton:pressed {
            background-color: #E0F2FE;
            border-color: #00B2FF;
        }
        QDialog QPushButton:pressed:focus {
            border-color: #0090DD;
        }
        QDialog QPushButton:default {
            color: #FFFFFF;
            background-color: #00B2FF;
            border: 1px solid #00B2FF;
            font-weight: 600;
        }
        QDialog QPushButton:default:hover {
            background-color: #33C4FF;
            border-color: #33C4FF;
            color: #FFFFFF;
        }
        QDialog QPushButton:default:focus {
            border: 2px solid #7DD3FC;
            background-color: #00B2FF;
            color: #FFFFFF;
        }
        QDialog QPushButton:default:hover:focus {
            background-color: #33C4FF;
            border-color: #BAE6FD;
            color: #FFFFFF;
        }
        QDialog QPushButton:default:pressed {
            background-color: #0090DD;
            border-color: #0090DD;
            color: #FFFFFF;
        }
        QDialog QPushButton:default:pressed:focus {
            border-color: #0369A1;
        }
        QDialog QTableWidget {
            background-color: #FFFFFF;
            alternate-background-color: #F0F7FF;
            color: #0F172A;
            gridline-color: #BFDBFE;
            border: 1px solid #BFDBFE;
            border-radius: 10px;
            font-size: 13px;
            selection-background-color: #E0F2FE;
            selection-color: #0F172A;
        }
        QDialog QTableWidget:focus {
            outline: none;
        }
        QDialog QTableWidget::item {
            color: #0F172A;
            padding: 4px;
        }
        QDialog QTableWidget::item:hover {
            background-color: #E0F2FE;
            color: #0F172A;
        }
        QDialog QTableWidget::item:selected {
            background-color: #DBEAFE;
            color: #0F172A;
        }
        QDialog QTableWidget::item:selected:hover {
            background-color: #BFDBFE;
            color: #0F172A;
        }
        QDialog QTableWidget QTableCornerButton::section {
            background-color: #EFF6FF;
            border: 1px solid #BFDBFE;
        }
        QDialog QTableWidget QHeaderView {
            background-color: #EFF6FF;
        }
        QDialog QTableWidget QHeaderView::section:horizontal {
            background-color: #EFF6FF;
            color: #0F172A;
            padding: 8px;
            border: none;
            border-bottom: 1px solid #BFDBFE;
            border-right: 1px solid #BFDBFE;
            font-size: 12px;
            font-weight: 600;
        }
        QDialog QTableWidget QHeaderView::section:vertical {
            background-color: #F8FAFF;
            color: #475569;
            padding: 4px;
            border: none;
            border-right: 1px solid #BFDBFE;
            border-bottom: 1px solid #BFDBFE;
            font-size: 12px;
        }
        QDialog QCheckBox {
            color: #0F172A;
            spacing: 6px;
        }
        QDialog QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 3px;
        }
        QDialog QCheckBox::indicator:unchecked {
            background-color: #FFFFFF;
            border: 1px solid #BFDBFE;
        }
        QDialog QCheckBox::indicator:unchecked:hover {
            border-color: #93C5FD;
        }
        QDialog QCheckBox::indicator:checked {
            image: url(:/add_window_checkbox_checked.svg);
            border: none;
        }
        QDialog QCheckBox::indicator:checked:hover {
            image: url(:/add_window_checkbox_checked_hover.svg);
        }
        QDialog QCheckBox::indicator:disabled {
            background-color: #EFF6FF;
            border-color: #BFDBFE;
        }
    )QSS");
}

QString ApplyStyle::addWindowDialogStyle(MainWindowTheme theme)
{
    Q_UNUSED(theme)
    return addWindowDialogStyleSkyQss()
        + baseControlStyleQss()
        + addWindowDialogComboBoxQss(MainWindowTheme::Default)
        + unifiedScrollBarQss(MainWindowTheme::Default);
}

QString ApplyStyle::addWindowDialogStyle()
{
    return addWindowDialogStyle(MainWindowTheme::Default);
}

static QString buildEditProfileDialogExtraQss(ApplyStyle::MainWindowTheme theme)
{
    QString avatarBorder;
    QString avatarBg;
    QString hintMuted;
    QString hintFaint;
    QString pteText;
    QString pteBg;
    QString pteBorder;
    QString pteFocus;
    QString pteSelBg;
    QString pteSelFg;
    switch (theme) {
    case ApplyStyle::MainWindowTheme::Cool:
        avatarBorder = QStringLiteral("#a8c9ef");
        avatarBg = QStringLiteral("#eef6fc");
        hintMuted = QStringLiteral("#475569");
        hintFaint = QStringLiteral("#64748b");
        pteText = QStringLiteral("#1e293b");
        pteBg = QStringLiteral("#ffffff");
        pteBorder = QStringLiteral("#c0d9f7");
        pteFocus = QStringLiteral("#8eb4e0");
        pteSelBg = QStringLiteral("#c0d9f7");
        pteSelFg = QStringLiteral("#1e293b");
        break;
    case ApplyStyle::MainWindowTheme::Warm:
        avatarBorder = QStringLiteral("#e5ddd4");
        avatarBg = QStringLiteral("#faf5ed");
        hintMuted = QStringLiteral("#6b6560");
        hintFaint = QStringLiteral("#78716c");
        pteText = QStringLiteral("#3d3429");
        pteBg = QStringLiteral("#fffcf9");
        pteBorder = QStringLiteral("#e5ddd4");
        pteFocus = QStringLiteral("#b45309");
        pteSelBg = QStringLiteral("#fef3c7");
        pteSelFg = QStringLiteral("#3d3429");
        break;
    case ApplyStyle::MainWindowTheme::Default:
    default:
        avatarBorder = QStringLiteral("#d4d4d8");
        avatarBg = QStringLiteral("#fafafa");
        hintMuted = QStringLiteral("#52525b");
        hintFaint = QStringLiteral("#71717a");
        pteText = QStringLiteral("#18181b");
        pteBg = QStringLiteral("#ffffff");
        pteBorder = QStringLiteral("#d4d4d8");
        pteFocus = QStringLiteral("#71717a");
        pteSelBg = QStringLiteral("#d4d4d8");
        pteSelFg = QStringLiteral("#18181b");
        break;
    }

    return QStringLiteral(
               R"QSS(
        QLabel#editProfileAvatar {
            border: 1px solid %1;
            border-radius: 8px;
            background-color: %2;
        }
        QLabel#editProfileHintMuted {
            color: %3;
            font-size: 12px;
            background: transparent;
        }
        QLabel#editProfileHintFaint {
            color: %4;
            font-size: 11px;
            background: transparent;
        }
        QDialog QPlainTextEdit#editProfileBio {
            color: %5;
            background-color: %6;
            border: 1px solid %7;
            border-radius: 8px;
            padding: 8px 10px;
            font-size: 13px;
            selection-background-color: %8;
            selection-color: %9;
        }
        QDialog QPlainTextEdit#editProfileBio:focus {
            border-color: %10;
        }
    )QSS")
        .arg(avatarBorder)
        .arg(avatarBg)
        .arg(hintMuted)
        .arg(hintFaint)
        .arg(pteText)
        .arg(pteBg)
        .arg(pteBorder)
        .arg(pteSelBg)
        .arg(pteSelFg)
        .arg(pteFocus);
}

QString ApplyStyle::editProfileDialogStyle(MainWindowTheme theme)
{
    theme = MainWindowTheme::Default;
    return addWindowDialogStyle(theme) + buildEditProfileDialogExtraQss(theme);
}

QString ApplyStyle::editProfileDialogStyle()
{
    return editProfileDialogStyle(MainWindowTheme::Default);
}

namespace {

struct AggregateChatTokens {
    const char* pageBg;
    const char* labelCombo;
    const char* panelGradTop;
    const char* panelGradBottom;
    const char* panelBorder;
    const char* accent;
    const char* accentHover;
    const char* accentPressed;
    const char* tabText;
    const char* tabHoverBg;
    const char* searchBorder;
    const char* searchBg;
    const char* searchText;
    const char* searchFocus;
    const char* listHover;
    const char* listSelected;
    const char* emptyIconBg;
    const char* emptyMain;
    const char* emptySub;
    const char* chatHeaderBg;
    const char* chatHeaderText;
    const char* chatHeaderBorder;
    const char* areaBg;
    const char* inputDivider;
    const char* inputBg;
    const char* inputBorder;
    const char* inputText;
    const char* inputFocus;
    const char* bubbleInBg;
    const char* bubbleInBorder;
    const char* bubbleOutBg;
    const char* bubbleTextIn;
    const char* bubbleTextOut;
    const char* bubbleMetaIn;
    const char* bubbleMetaOut;
    const char* dateLine;
    const char* dateText;
    const char* rightEmptyG1;
    const char* rightEmptyG2;
    const char* rightText;
    /** 聚合底部输入浮层背景（半透明） */
    const char* inputPanelGlass;
    /** 聚合输入框在半透明条上的背景 */
    const char* inputMessageGlass;
};

/* 默认聚合：整体浅灰蓝工作区（参考 IM 工作台），天蓝 outgoing / 白底 incoming 气泡 */
const AggregateChatTokens kAggDefault = {
    "#EEF6FC",
    "#0F172A",
    "#FFFFFF",
    "#F4F6FA",
    "#E5E7EB",
    "#20B8E8",
    "#35C5F3",
    "#149DCA",
    "#64748B",
    "rgba(0,178,255,0.10)",
    "#D1D5DB",
    "#FFFFFF",
    "#111827",
    "#7DD3FC",
    "rgba(15,23,42,0.05)",
    "rgba(0,178,255,0.14)",
    "#DFF2FC",
    "#111827",
    "#64748B",
    "#FFFFFF",
    "#111827",
    "#E5E7EB",
    "#F4F6FA",
    "#E5E7EB",
    "#FFFFFF",
    "#E5E7EB",
    "#111827",
    "#20B8E8",
    "#FFFFFF",
    "#E8EAED",
    "#20B8E8",
    "#111827",
    "#FFFFFF",
    "#64748B",
    "rgba(255,255,255,0.88)",
    "#E5E7EB",
    "#64748B",
    "#E5E7EB",
    "#F4F6FA",
    "#64748B",
    "rgba(255,255,255,0.82)",
    "rgba(255,255,255,0.94)",
};

const AggregateChatTokens kAggCool = {
    "#e2e8f0",
    "#0f172a",
    "#cbd5e1",
    "#e2e8f0",
    "#94a3b8",
    "#0284c7",
    "#0ea5e9",
    "#0369a1",
    "#0f172a",
    "rgba(2,132,199,0.12)",
    "#94a3b8",
    "rgba(255,255,255,0.92)",
    "#0f172a",
    "#0284c7",
    "rgba(148,163,184,0.35)",
    "#93c5fd",
    "#64748b",
    "#1e293b",
    "#64748b",
    "#e2e8f0",
    "#0f172a",
    "#cbd5e1",
    "#e2e8f0",
    "#cbd5e1",
    "#ffffff",
    "#94a3b8",
    "#0f172a",
    "#0284c7",
    "#bfdbfe",
    "#60a5fa",
    "#0284c7",
    "#0f172a",
    "#ffffff",
    "#475569",
    "rgba(255, 255, 255, 0.8)",
    "#cbd5e1",
    "#64748b",
    "#f1f5f9",
    "#e2e8f0",
    "#0f172a",
    "rgba(255,255,255,0.72)",
    "rgba(255,255,255,0.92)",
};

const AggregateChatTokens kAggWarm = {
    "#f5efe6",
    "#3d3429",
    "#e8dfd4",
    "#f5efe6",
    "#d6cbc0",
    "#b45309",
    "#c05621",
    "#9a3412",
    "#3d3429",
    "rgba(180,83,9,0.12)",
    "#d6cbc0",
    "rgba(255,252,249,0.95)",
    "#3d3429",
    "#b45309",
    "rgba(214,203,192,0.45)",
    "#fcd34d",
    "#f59e0b",
    "#3d3429",
    "#78716c",
    "#f5efe6",
    "#3d3429",
    "#d6cbc0",
    "#f5efe6",
    "#e5ddd4",
    "#fffcf9",
    "#d6cbc0",
    "#3d3429",
    "#b45309",
    "#fde68a",
    "#f59e0b",
    "#b45309",
    "#3d3429",
    "#fffaf5",
    "#78716c",
    "rgba(255, 250, 245, 0.9)",
    "#d6cbc0",
    "#78716c",
    "#faf5ed",
    "#ebe4db",
    "#3d3429",
    "rgba(255,252,249,0.82)",
    "rgba(255,255,250,0.92)",
};

/** 按 %1…%44 编号替换；须从高到低替换，避免 %1 误伤 %11 等。链式 QString::arg 会每次填「当前最小编号」，
 *  模板若缺 %3/%4 等会导致后续颜色整体错位（聚合页大面积发黑）。 */
static void replaceAggregateChatPlaceholders(QString& qss, const AggregateChatTokens& t)
{
    const char* const vals[44] = {
        t.pageBg,
        t.labelCombo,
        t.panelGradTop,
        t.panelGradBottom,
        t.panelBorder,
        t.accent,
        t.accentHover,
        t.accentPressed,
        t.tabText,
        t.tabHoverBg,
        t.searchBorder,
        t.searchBg,
        t.searchText,
        t.searchFocus,
        t.listHover,
        t.listSelected,
        t.areaBg,
        t.emptyIconBg,
        t.emptyMain,
        t.emptySub,
        t.chatHeaderBg,
        t.chatHeaderText,
        t.chatHeaderBorder,
        t.inputDivider,
        t.inputBg,
        t.inputBorder,
        t.inputText,
        t.inputFocus,
        t.bubbleInBg,
        t.bubbleInBorder,
        t.bubbleOutBg,
        t.bubbleTextIn,
        t.bubbleTextOut,
        t.bubbleMetaIn,
        t.bubbleMetaOut,
        t.dateLine,
        t.dateText,
        t.rightText,
        t.rightEmptyG1,
        t.rightEmptyG2,
        t.bubbleMetaIn,
        t.chatHeaderText,
        t.inputPanelGlass,
        t.inputMessageGlass,
    };
    for (int n = 44; n >= 1; --n) {
        qss.replace(QLatin1Char('%') + QString::number(n), QLatin1String(vals[n - 1]));
    }
}

QString buildAggregateChatFormQss(const AggregateChatTokens& t)
{
    QString qss = QStringLiteral(
        R"QSS(
        AggregateChatForm {
            background: %1;
        }
        AggregateChatForm QPushButton:focus,
        AggregateChatForm QToolButton:focus,
        AggregateChatForm QComboBox:focus,
        AggregateChatForm QLineEdit:focus,
        AggregateChatForm QPlainTextEdit:focus {
            outline: none;
        }
        QLabel {
            color: %2;
        }
        QLabel#aggregatePlatformSectionTitle {
            color: %2;
            font-size: 16px;
            font-weight: 700;
        }
        QLabel#aggregateModeLabel {
            color: %2;
            font-size: 12px;
            font-weight: 600;
        }
        QFrame#aggregatePlatformListenBox {
            background: %12;
            border: 1px solid %11;
            border-radius: 10px;
        }
        QCheckBox#aggregatePlatformListenCheck {
            color: %13;
            font-size: 12px;
            spacing: 5px;
        }
        QLabel#aggregatePlatformListenStatus {
            color: %9;
            font-size: 11px;
        }
        QPushButton#aggregatePlatformListenStartButton,
        QPushButton#aggregatePlatformListenStopButton {
            background: %16;
            color: %13;
            border: 1px solid transparent;
            border-radius: 8px;
            padding: 5px 8px;
            font-size: 12px;
            font-weight: 500;
        }
        QPushButton#aggregatePlatformListenStartButton:hover,
        QPushButton#aggregatePlatformListenStopButton:hover {
            background: %17;
            border-color: %14;
        }
        QPushButton#aggregatePlatformListenStartButton:pressed,
        QPushButton#aggregatePlatformListenStopButton:pressed {
            background: %15;
        }
        AggregateChatForm QComboBox {
            background: %12;
            border: 1px solid %11;
            border-radius: 8px;
            padding: 4px 10px 4px 10px;
            min-height: 24px;
            font-size: 12px;
            color: %13;
        }
        AggregateChatForm QComboBox:hover {
            border-color: %14;
            background: %17;
        }
        AggregateChatForm QComboBox:focus {
            border-color: %14;
            background: %17;
        }
        AggregateChatForm QComboBox:disabled {
            color: %20;
            background: %15;
            border-color: %11;
        }
        AggregateChatForm QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 26px;
            border: none;
            border-left: 1px solid %11;
            border-top-right-radius: 7px;
            border-bottom-right-radius: 7px;
            background: transparent;
        }
        AggregateChatForm QComboBox::down-arrow {
            image: url(:/fold_arrow_to_expand_icon.svg);
            width: 16px;
            height: 16px;
            margin-right: 6px;
        }
        AggregateChatForm QComboBox[popupOpen="true"]::down-arrow {
            image: url(:/fold_arrow_to_collapse_icon.svg);
        }
        AggregateChatForm QComboBox QAbstractItemView {
            background: %12;
            color: %13;
            selection-background-color: %16;
            selection-color: %13;
            border: 1px solid %11;
            outline: none;
            padding: 2px;
        }
        QWidget#aggregateLeftPanel {
            background: #F0F6FC;
            border-right: 1px solid %5;
        }
        QWidget#aggregateLeftToolBar {
            background: #DCE9F6;
            border-right: 1px solid %5;
        }
        QToolButton#aggregateToolBarButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 8px;
        }
        QToolButton#aggregateToolBarButton:checked {
            background: %16;
            border-color: %14;
        }
        QToolButton#aggregateToolBarButton:hover:!checked {
            background: %10;
            border-color: %11;
        }
        QToolButton#aggregateToolBarButton:pressed {
            background: %9;
        }
        QStackedWidget#aggregateLeftStack,
        QStackedWidget#aggregateCenterStack,
        QWidget#aggregateCenterEmptyState,
        QWidget#messageScrollViewport {
            background: %17;
        }
        QPushButton#simulateButton {
            background: transparent;
            color: %13;
            border: 1px solid transparent;
            border-radius: 19px;
            padding: 0;
            font-size: 12px;
            font-weight: 500;
        }
        QPushButton#simulateButton:hover {
            background: %17;
            border-color: %14;
        }
        QPushButton#simulateButton:pressed {
            background: %15;
        }
        QPushButton#simulateButton:disabled {
            background: transparent;
            border-color: transparent;
            color: %20;
        }
        QPushButton#aggregateAutoReplyToggleButton {
            background: #FFFFFF;
            color: %13;
            border: 1px solid #3AA5FF;
            border-radius: 19px;
            padding: 0;
            min-width: 38px;
            max-width: 38px;
            min-height: 38px;
            max-height: 38px;
            outline: none;
            font-size: 12px;
            font-weight: 500;
        }
        QPushButton#aggregateAutoReplyToggleButton:focus {
            background: #FFFFFF;
            border: 1px solid #3AA5FF;
            border-radius: 19px;
            outline: none;
        }
        QPushButton#aggregateAutoReplyToggleButton:hover {
            background: #EFF8FF;
            border-color: #168EEA;
        }
        QPushButton#aggregateAutoReplyToggleButton:pressed {
            background: #DBF0FF;
        }
        QPushButton#aggregateAutoReplyToggleButton:checked {
            background: #EAF6FF;
            color: #168EEA;
            border-color: #168EEA;
            border-radius: 19px;
            outline: none;
        }
        QPushButton#aggregateAutoReplyToggleButton:checked:hover {
            background: #DBF0FF;
            border-color: #0B7ED0;
        }
        QPushButton#aggregateAutoReplyToggleButton:disabled {
            background: #FFFFFF;
            border-color: %11;
            color: %20;
        }
        QPushButton#aggregateTabAll,
        QPushButton#aggregateTabPending,
        QPushButton#aggregateTabReplied {
            background: transparent;
            color: %9;
            border: 1px solid transparent;
            border-radius: 8px;
            padding: 5px 12px;
            font-size: 12px;
            font-weight: 500;
        }
        QPushButton#aggregateTabAll:checked,
        QPushButton#aggregateTabPending:checked,
        QPushButton#aggregateTabReplied:checked {
            background: %16;
            color: %13;
            border: 1px solid transparent;
        }
        QPushButton#aggregateTabAll:!checked,
        QPushButton#aggregateTabPending:!checked,
        QPushButton#aggregateTabReplied:!checked {
            background: transparent;
            color: %9;
        }
        QPushButton#aggregateTabAll:hover:!checked,
        QPushButton#aggregateTabPending:hover:!checked,
        QPushButton#aggregateTabReplied:hover:!checked {
            background: %15;
            border-color: %11;
        }
        QLineEdit#aggregateSearch {
            padding: 6px 8px;
            border: 1px solid %11;
            border-radius: 8px;
            font-size: 13px;
            background: %12;
            color: %13;
        }
        QLineEdit#aggregateSearch:hover {
            border-color: %14;
            background: %17;
        }
        QLineEdit#aggregateSearch:focus {
            border-color: %14;
            background: %17;
        }

    )QSS" R"QSS(
        QWidget#aggregateListEmpty {
            background: transparent;
        }
        QLabel#aggregateListEmptyText {
            font-size: 12px;
            color: %2;
        }
        QListWidget#aggregateConversationList,
        QListView#aggregateConversationList {
            background: transparent;
            border: none;
            outline: none;
        }
        QListWidget#aggregateConversationList:focus,
        QListView#aggregateConversationList:focus {
            outline: none;
        }
        QListWidget#aggregateConversationList::item,
        QListView#aggregateConversationList::item {
            padding: 2px 0;
            border-radius: 4px;
            outline: none;
            border: none;
        }
        QListWidget#aggregateConversationList::item:focus,
        QListView#aggregateConversationList::item:focus {
            outline: none;
        }
        QListWidget#aggregateConversationList::item:hover,
        QListView#aggregateConversationList::item:hover {
            background: rgba(32, 184, 232, 0.08);
        }
        QListWidget#aggregateConversationList::item:selected,
        QListView#aggregateConversationList::item:selected {
            background: transparent;
        }
        QWidget#convItemWidget {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 4px;
            outline: none;
        }
        QWidget#convItemWidget[selected="true"] {
            background: #DFF2FC;
            border: 1px solid transparent;
        }
        QLabel#convItemName {
            color: %13;
            font-size: 13px;
            font-weight: 600;
        }
        QLabel#convItemTime {
            color: %20;
            font-size: 11px;
        }
        QLabel#convItemPreview {
            color: #6B7280;
            font-size: 12px;
        }
        QWidget#convItemWidget[selected="true"] QLabel#convItemPreview {
            color: #3F4B5A;
        }
        QLabel#unreadBadge {
            background: %6;
            color: white;
            font-size: 11px;
            font-weight: 700;
            border-radius: 10px;
        }

    )QSS" R"QSS(
        QWidget#aggregateCenterPanel {
            background: #F4F6FA;
        }
        QWidget#chatArea {
            background: #F4F6FA;
        }
        /* 与 syncSolidBackgrounds 中左栏/窗体底色 %1 一致，避免 handle 用 %17 出现竖向「假缝」 */
        QSplitter {
            background: %1;
        }
        QSplitter::handle {
            background: %1;
            border: none;
        }
        QLabel#aggregateEmptyMain {
            font-size: 16px;
            font-weight: bold;
            color: %19;
        }
        QLabel#aggregateEmptySub {
            font-size: 12px;
            color: %20;
        }

        QWidget#chatHeader {
            background: #FFFFFF;
            font-size: 14px;
            font-weight: bold;
            color: %22;
            border-bottom: 1px solid %23;
            /* 全宽条贴中间栏；文字与内容区同宽（与下方输入区内边距 16 对齐） */
            padding: 0;
        }
        QLabel#chatHeaderTitle {
            background: transparent;
            font-size: 14px;
            font-weight: bold;
            color: %22;
            border: none;
            padding: 0;
        }
        QToolButton#aggregateConversationBackButton {
            background: transparent;
            color: %22;
            border: none;
            border-radius: 6px;
            font-size: 18px;
            font-weight: 700;
            padding: 0;
        }
        QToolButton#aggregateConversationBackButton:hover {
            background: %17;
        }
        QToolButton#aggregateConversationBackButton:pressed {
            background: %15;
        }
        QScrollArea#messageScroll {
            background: #F4F6FA;
            border: none;
        }
        QScrollArea#messageScroll QWidget {
            background: transparent;
        }
        QFrame#inputDivider {
            color: %24;
        }
        QWidget#aggregateChatInputOverlayHost {
            background: #F4F6FA;
        }
        QWidget#aggregateChatInputPanel {
            background: #FFFFFF;
            border-top: 1px solid %26;
        }
        QWidget#aggregateComposeBox {
            background: #FFFFFF;
            border: 1px solid %26;
            border-radius: 18px;
        }
        QScrollArea#composeAttachmentsScroll {
            background: #FFFFFF;
            border: none;
        }
        QWidget#composeAttachmentsViewport,
        QWidget#composeAttachmentsWidget {
            background: #FFFFFF;
        }
        QPlainTextEdit#messageInput {
            background: transparent;
            border: none;
            border-radius: 0;
            padding: 4px 4px 2px 4px;
            font-size: 13px;
            color: %27;
        }
        QPlainTextEdit#messageInput:hover {
            border: none;
        }
        QPlainTextEdit#messageInput:focus {
            border: none;
        }
        QPlainTextEdit#messageInput:disabled {
            color: %20;
            background: transparent;
        }
        QToolButton#aggregateAiModelButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 10px;
            padding: 4px 22px 4px 10px;
            min-height: 28px;
            max-height: 34px;
            font-size: 11px;
            font-weight: 500;
            color: %20;
        }
        QToolButton#aggregateAiModelButton:hover {
            border-color: %11;
            background: %17;
        }
        QToolButton#aggregateAiModelButton:pressed {
            background: %15;
        }
        QToolButton#aggregateAiModelButton:disabled {
            color: %20;
            background: transparent;
            border-color: transparent;
        }
        QToolButton#aggregateAiModelButton::menu-indicator {
            image: url(:/fold_arrow_to_expand_icon.svg);
            width: 12px;
            height: 12px;
            subcontrol-position: center right;
            subcontrol-origin: padding;
            right: 6px;
        }
        QPushButton#sendButton {
            background: %6;
            color: white;
            border: none;
            border-radius: 19px;
            padding: 0;
            font-size: 13px;
            font-weight: bold;
        }
        QPushButton#sendButton:hover {
            background: %7;
        }
        QPushButton#sendButton:pressed {
            background: %8;
        }
        QPushButton#sendButton:disabled {
            background: %15;
            color: %20;
        }

        QWidget#chatArea QFrame#bubbleIn {
            background: #FFFFFF;
            border: 1px solid #E5E7EB;
            border-radius: 12px;
        }
        QWidget#chatArea QFrame#bubbleOut {
            background: #20B8E8;
            border: none;
            border-radius: 12px;
        }
        QLabel#bubbleTextIn {
            font-size: 13px;
            color: #111827;
        }
        QLabel#bubbleTextOut {
            font-size: 13px;
            color: #FFFFFF;
        }
        QLabel#bubbleMetaIn {
            font-size: 10px;
            color: #9CA3AF;
        }
        QLabel#bubbleMetaOut {
            font-size: 10px;
            color: #E0F2FE;
        }
        QLabel#bubbleOutSenderTime {
            font-size: 10px;
            color: %41;
            background: transparent;
        }
        QLabel#bubbleOutSenderNick {
            font-size: 11px;
            font-weight: 500;
            color: %42;
            background: transparent;
        }
        QLabel#bubbleOutAvatar {
            background: transparent;
            border-radius: 18px;
        }
        QLabel#bubbleInAvatar {
            background: transparent;
            border-radius: 18px;
        }
        QFrame#dateSeparatorLine {
            background: %36;
            border: none;
        }
        QLabel#dateSeparatorText {
            font-size: 11px;
            color: %37;
            background: transparent;
        }

    )QSS" R"QSS(
        QWidget#aggregateRightPanel {
            background: #F7F9FB;
            border-left: 1px solid %5;
        }
        QWidget#aggregateModelPickerOverlay {
            background: rgba(15, 23, 42, 0.16);
        }
        QWidget#aggregateModelPickerSheet {
            background: #F7F9FB;
            border-left: 1px solid %5;
        }
        QToolButton#aggregateModelPickerBack {
            background: transparent;
            color: %6;
            border: none;
            border-radius: 6px;
            padding: 4px 8px;
            font-size: 12px;
        }
        QToolButton#aggregateModelPickerBack:hover {
            background: %17;
        }
        QLabel#aggregateModelPickerTitle {
            font-size: 13px;
            font-weight: 600;
            color: %20;
        }
        QListWidget#aggregateModelPickerList {
            background: #FFFFFF;
            border: 1px solid %11;
            border-radius: 8px;
            padding: 4px 0;
            color: %20;
            font-size: 12px;
        }
        QListWidget#aggregateModelPickerList::item {
            padding: 8px 10px;
            min-height: 32px;
        }
        QListWidget#aggregateModelPickerList::item:selected {
            background: %16;
            color: %13;
        }
        QListWidget#aggregateModelPickerList::item:hover:!selected {
            background: %15;
        }
        QScrollArea#aggregateRightBarScroll {
            background: #F7F9FB;
            border: none;
        }
        QWidget#aggregateRightBarScrollViewport {
            background: #F7F9FB;
        }
        QWidget#aggregateRightBarScrollContent {
            background: #F7F9FB;
            color: #111827;
        }
        QWidget#aggregateRightBarModelBlock {
            background: transparent;
        }
        QWidget#aggregateRightBarMetricsWrap {
            background: transparent;
        }
        QWidget#aggregateSendTimelineHeaderRow {
            background: transparent;
        }
        QFrame#aggregateRightBarModelIcon {
            background: #FFFFFF;
            border: 1px solid %5;
            border-radius: 12px;
        }
        QWidget#aggregateRightBarModelStatusDot {
            background: #22C55E;
            border-radius: 4px;
        }
        QLabel#aggregateRightBarModelName {
            font-size: 15px;
            font-weight: 600;
            color: #0F172A;
            background: transparent;
        }
        QPushButton#aggregateModelSwitchRowButton {
            background: #FFFFFF;
            color: #334155;
            border: 1px solid %5;
            border-radius: 8px;
            padding: 8px 10px;
            font-size: 12px;
            font-weight: 500;
            text-align: center;
        }
        QPushButton#aggregateModelSwitchRowButton:hover {
            background: %17;
            border-color: %11;
        }
        QPushButton#aggregateModelSwitchRowButton:pressed {
            background: %15;
        }
        QFrame#aggregateRightBarBlockSep {
            background: %5;
            border: none;
            min-height: 1px;
            max-height: 1px;
        }
        QLabel#aggregateRightBarMetricsTitle {
            font-size: 12px;
            font-weight: 600;
            color: %20;
        }
        QFrame#aggregateRightBarMetricCard {
            background: #FFFFFF;
            border: 1px solid #E5E7EB;
            border-radius: 16px;
        }
        QFrame#aggregateRightBarMetricIconWrap {
            border: none;
            border-radius: 20px;
        }
        QFrame#aggregateRightBarMetricIconWrap[metricKey="runtime"] { background: rgba(59, 130, 246, 0.12); }
        QFrame#aggregateRightBarMetricIconWrap[metricKey="request"] { background: rgba(234, 179, 8, 0.16); }
        QFrame#aggregateRightBarMetricIconWrap[metricKey="system"] { background: rgba(22, 163, 74, 0.12); }
        QFrame#aggregateRightBarMetricIconWrap[metricKey="response"] { background: rgba(244, 63, 94, 0.12); }
        QLabel#aggregateRightBarMetricIcon {
            background: transparent;
        }
        QLabel#aggregateRightBarMetricValue {
            font-size: 18px;
            font-weight: 700;
            color: %19;
        }
        QLabel#aggregateRightBarMetricCaption {
            font-size: 12px;
            font-weight: 500;
            color: #6B7280;
        }
        QStackedWidget#aggregateRightStack {
            background: #F7F9FB;
        }
        QToolButton#aggregateSendTimelineToggle {
            background: transparent;
            border: none;
            border-radius: 0;
            padding: 0 0 8px 0;
            text-align: left;
            color: #111827;
            font-size: 12px;
            font-weight: 600;
        }
        QToolButton#aggregateSendTimelineToggle:hover {
            color: #0F172A;
        }
        QToolButton#aggregateSendTimelineToggle:pressed {
            color: #1E293B;
        }
        QToolButton#aggregateCustomerProfileToggle {
            background: transparent;
            border: none;
            border-radius: 0;
            padding: 0 0 8px 0;
            text-align: left;
            color: #111827;
            font-size: 12px;
            font-weight: 600;
        }
        QToolButton#aggregateCustomerProfileToggle:hover {
            color: #0F172A;
        }
        QPushButton#aggregateCustomerProfileOrganizeButton {
            background: #FFFFFF;
            color: #334155;
            border: 1px solid %26;
            border-radius: 6px;
            font-size: 12px;
            font-weight: 500;
            padding: 3px 8px;
        }
        QPushButton#aggregateCustomerProfileOrganizeButton:hover {
            background: %17;
            border-color: %14;
        }
        QPushButton#aggregateCustomerProfileOrganizeButton:disabled {
            background: %15;
            border-color: %11;
            color: %20;
        }
        QLabel#aggregateCustomerProfileText {
            background: #FFFFFF;
            border: 1px solid %26;
            border-radius: 8px;
            padding: 8px;
            font-size: 12px;
            color: %27;
            line-height: 145%;
        }
        QPlainTextEdit#sendStatusTimeline {
            background: #FFFFFF;
            border: 1px solid %26;
            border-radius: 8px;
            padding: 8px;
            font-size: 12px;
            color: %27;
        }
        QPlainTextEdit#sendStatusTimeline:focus {
            border-color: %28;
        }
        QLabel#aggregateInlineStatus {
            min-height: 16px;
            color: %20;
            font-size: 11px;
            padding: 0 4px;
            background: transparent;
        }
        QToolButton#sendTimelineClearBtn {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 6px;
            padding: 0;
            min-width: 28px;
            min-height: 28px;
        }
        QToolButton#sendTimelineClearBtn:hover {
            background: %17;
            border-color: %11;
        }
        QToolButton#sendTimelineClearBtn:pressed {
            background: %15;
            border-color: %11;
        }

    )QSS");
    replaceAggregateChatPlaceholders(qss, t);
    return qss;
}

} // namespace

QString ApplyStyle::aggregateChatFormStyle()
{
    return aggregateChatFormStyle(MainWindowTheme::Default);
}

QString ApplyStyle::aggregateChatFormStyle(MainWindowTheme theme)
{
    Q_UNUSED(theme)
    return buildAggregateChatFormQss(kAggDefault)
        + baseControlStyleQss()
        + unifiedScrollBarQss(MainWindowTheme::Default);
}

QString ApplyStyle::robotAssistantExtraStyle(MainWindowTheme theme)
{
    theme = MainWindowTheme::Default;
    switch (theme) {
    case MainWindowTheme::Cool:
        return QStringLiteral(
            R"QSS(
            RobotAssistantWidget QTabWidget::pane {
                border: none;
                background: transparent;
            }
            RobotAssistantWidget QTabBar::tab {
                padding: 10px 22px;
                font-size: 13px;
                color: #64748b;
                background: transparent;
                border: none;
                border-bottom: 2px solid transparent;
            }
            RobotAssistantWidget QTabBar::tab:selected {
                color: #0f172a;
                font-weight: 600;
                border-bottom: 2px solid #0284c7;
            }
            RobotAssistantWidget QTabBar::tab:hover:!selected {
                color: #334155;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage {
                background: #f1f5f9;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLabel#robotSettingsFieldLabel {
                color: #0f172a;
                font-size: 12px;
                font-weight: 600;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLabel {
                color: #0f172a;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLineEdit#robotSettingsField {
                background: #ffffff;
                border: 1px solid #94a3b8;
                border-radius: 8px;
                padding: 8px 10px;
                font-size: 13px;
                color: #0f172a;
                min-height: 22px;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLineEdit#robotSettingsField:focus {
                border-color: #0284c7;
            }
            RobotAssistantWidget QComboBox#robotAssistantModelCombo {
                background: #ffffff;
                border: 1px solid #94a3b8;
                border-radius: 8px;
                padding: 4px 10px;
                padding-right: 28px;
                font-size: 13px;
                color: #0f172a;
                min-height: 22px;
                min-width: 168px;
            }
            RobotAssistantWidget QComboBox#robotAssistantModelCombo:focus {
                border-color: #0284c7;
            }
            RobotAssistantWidget QComboBox#robotAssistantModelCombo::drop-down {
                border: none;
                width: 24px;
            }
            RobotAssistantWidget QLabel#robotAssistantAvatarIn {
                background: transparent;
                border-radius: 18px;
            }
            RobotAssistantWidget QLabel#robotAssistantPrivacy {
                font-size: 12px;
                color: #475569;
            }
            RobotAssistantWidget QLabel#robotAssistantStatus {
                font-size: 12px;
                color: #94a3b8;
            }
            )QSS");
    case MainWindowTheme::Warm:
        return QStringLiteral(
            R"QSS(
            RobotAssistantWidget QTabWidget::pane {
                border: none;
                background: transparent;
            }
            RobotAssistantWidget QTabBar::tab {
                padding: 10px 22px;
                font-size: 13px;
                color: #78716c;
                background: transparent;
                border: none;
                border-bottom: 2px solid transparent;
            }
            RobotAssistantWidget QTabBar::tab:selected {
                color: #3d3429;
                font-weight: 600;
                border-bottom: 2px solid #b45309;
            }
            RobotAssistantWidget QTabBar::tab:hover:!selected {
                color: #57534e;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage {
                background: #fffaf5;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLabel#robotSettingsFieldLabel {
                color: #3d3429;
                font-size: 12px;
                font-weight: 600;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLabel {
                color: #3d3429;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLineEdit#robotSettingsField {
                background: #ffffff;
                border: 1px solid #d6cbc0;
                border-radius: 8px;
                padding: 8px 10px;
                font-size: 13px;
                color: #3d3429;
                min-height: 22px;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLineEdit#robotSettingsField:focus {
                border-color: #b45309;
            }
            RobotAssistantWidget QComboBox#robotAssistantModelCombo {
                background: #ffffff;
                border: 1px solid #d6cbc0;
                border-radius: 8px;
                padding: 4px 10px;
                padding-right: 28px;
                font-size: 13px;
                color: #3d3429;
                min-height: 22px;
                min-width: 168px;
            }
            RobotAssistantWidget QComboBox#robotAssistantModelCombo:focus {
                border-color: #b45309;
            }
            RobotAssistantWidget QComboBox#robotAssistantModelCombo::drop-down {
                border: none;
                width: 24px;
            }
            RobotAssistantWidget QLabel#robotAssistantAvatarIn {
                background: transparent;
                border-radius: 18px;
            }
            RobotAssistantWidget QLabel#robotAssistantPrivacy {
                font-size: 12px;
                color: #78716c;
            }
            RobotAssistantWidget QLabel#robotAssistantStatus {
                font-size: 12px;
                color: #78716c;
            }
            )QSS");
    case MainWindowTheme::Default:
    default:
        return QStringLiteral(
            R"QSS(
            RobotAssistantWidget QTabWidget::pane {
                border: none;
                background: transparent;
            }
            RobotAssistantWidget QTabBar::tab {
                padding: 10px 22px;
                font-size: 13px;
                color: #71717a;
                background: transparent;
                border: none;
                border-bottom: 2px solid transparent;
            }
            RobotAssistantWidget QTabBar::tab:selected {
                color: #18181b;
                font-weight: 600;
                border-bottom: 2px solid #71717a;
            }
            RobotAssistantWidget QTabBar::tab:hover:!selected {
                color: #3f3f46;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage {
                background: #ffffff;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsHeader {
                background: transparent;
                border-bottom: 1px solid #e4e4e7;
                margin-bottom: 4px;
                padding-bottom: 4px;
            }
            RobotAssistantWidget QLabel#robotAssistantSettingsTitle {
                color: #18181b;
                font-size: 13px;
                font-weight: 600;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLabel#robotSettingsFieldLabel {
                color: #18181b;
                font-size: 12px;
                font-weight: 600;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLabel {
                color: #18181b;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLineEdit#robotSettingsField {
                background: #ffffff;
                border: 1px solid #d4d4d8;
                border-radius: 8px;
                padding: 8px 10px;
                font-size: 13px;
                color: #18181b;
                min-height: 22px;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLineEdit#robotSettingsField:hover {
                border-color: #a1a1aa;
                background: #fafafa;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLineEdit#robotSettingsField:focus {
                border-color: #71717a;
                background: #fafafa;
            }
            RobotAssistantWidget QWidget#robotAssistantSettingsPage QLineEdit#robotSettingsField:disabled {
                color: #71717a;
                background: #f4f4f5;
            }
            RobotAssistantWidget QComboBox#robotAssistantModelCombo {
                background: #ffffff;
                border: 1px solid #d4d4d8;
                border-radius: 8px;
                padding: 4px 10px;
                padding-right: 28px;
                font-size: 12px;
                color: #18181b;
                min-height: 22px;
                min-width: 144px;
            }
            RobotAssistantWidget QComboBox#robotAssistantModelCombo:hover {
                border-color: #a1a1aa;
                background: #fafafa;
            }
            RobotAssistantWidget QComboBox#robotAssistantModelCombo:focus {
                border-color: #71717a;
                background: #fafafa;
            }
            RobotAssistantWidget QComboBox#robotAssistantModelCombo:disabled {
                color: #71717a;
                background: #f4f4f5;
            }
            RobotAssistantWidget QComboBox#robotAssistantModelCombo::drop-down {
                border: none;
                width: 24px;
            }
            RobotAssistantWidget QComboBox#robotAssistantModelCombo QAbstractItemView {
                background: #ffffff;
                color: #18181b;
                border: 1px solid #d4d4d8;
                outline: none;
                selection-background-color: #eaf2ff;
                selection-color: #18181b;
            }
            RobotAssistantWidget QToolButton#robotAssistantAttachBtn {
                background: transparent;
                border: 1px solid transparent;
                border-radius: 8px;
            }
            RobotAssistantWidget QToolButton#robotAssistantAttachBtn:hover {
                background: #f5f7fb;
                border-color: #e4e4e7;
            }
            RobotAssistantWidget QToolButton#robotAssistantAttachBtn:pressed {
                background: #eef2f7;
                border-color: #d4d4d8;
            }
            RobotAssistantWidget QToolButton#robotAssistantAttachBtn:disabled {
                background: transparent;
                border-color: transparent;
            }
            RobotAssistantWidget QLabel#robotPendingThumb {
                background: #fafafa;
                border: 1px solid #e4e4e7;
                border-radius: 8px;
            }
            RobotAssistantWidget QLabel#robotPendingName {
                color: #3f3f46;
                font-size: 12px;
            }
            RobotAssistantWidget QLabel#robotAssistantAvatarIn {
                background: transparent;
                border-radius: 18px;
            }
            RobotAssistantWidget QLabel#robotAssistantPrivacy {
                font-size: 12px;
                color: #71717a;
            }
            RobotAssistantWidget QLabel#robotAssistantStatus {
                font-size: 12px;
                color: #71717a;
                min-height: 18px;
            }
            )QSS");
    }
}

QString ApplyStyle::helpDialogHtmlBodyTextColor(MainWindowTheme theme)
{
    theme = MainWindowTheme::Default;
    switch (theme) {
    case MainWindowTheme::Cool:
        return QStringLiteral("#e2e8f0");
    case MainWindowTheme::Warm:
        return QStringLiteral("#3d3429");
    case MainWindowTheme::Default:
    default:
        return QStringLiteral("#18181B");
    }
}

QString ApplyStyle::helpDialogHtmlHrBorderColor(MainWindowTheme theme)
{
    theme = MainWindowTheme::Default;
    switch (theme) {
    case MainWindowTheme::Cool:
        return QStringLiteral("#0284c7");
    case MainWindowTheme::Warm:
        return QStringLiteral("#b45309");
    case MainWindowTheme::Default:
    default:
        return QStringLiteral("#E4E4E7");
    }
}

QString ApplyStyle::helpDialogHtmlWarningColor(MainWindowTheme theme)
{
    theme = MainWindowTheme::Default;
    switch (theme) {
    case MainWindowTheme::Cool:
        return QStringLiteral("#fde68a");
    case MainWindowTheme::Warm:
        return QStringLiteral("#fb923c");
    case MainWindowTheme::Default:
    default:
        return QStringLiteral("#f59e0b");
    }
}

QString ApplyStyle::helpDialogStyle()
{
    return helpDialogStyle(MainWindowTheme::Default);
}

QString ApplyStyle::sidebarTocTreeStyleSheet(const QString& treeObjectName, MainWindowTheme theme)
{
    QString bg;
    QString border;
    QString text;
    QString selBg;
    QString selFg;
    QString hovBg;
    QString hovFg;
    switch (theme) {
    case MainWindowTheme::Cool:
        bg = QStringLiteral("#111827");
        border = QStringLiteral("#334155");
        text = QStringLiteral("#e2e8f0");
        selBg = QStringLiteral("#0284c7");
        selFg = QStringLiteral("#f8fafc");
        hovBg = QStringLiteral("#1e293b");
        hovFg = QStringLiteral("#f1f5f9");
        break;
    case MainWindowTheme::Warm:
        bg = QStringLiteral("#2a2624");
        border = QStringLiteral("#5c4f42");
        text = QStringLiteral("#f5efe6");
        selBg = QStringLiteral("#b45309");
        selFg = QStringLiteral("#fffbeb");
        hovBg = QStringLiteral("#3d3835");
        hovFg = QStringLiteral("#fff7ed");
        break;
    case MainWindowTheme::Default:
    default:
        bg = QStringLiteral("#F4F4F5");
        border = QStringLiteral("#D4D4D8");
        text = QStringLiteral("#27272A");
        selBg = QStringLiteral("#3F3F46");
        selFg = QStringLiteral("#FAFAFA");
        hovBg = QStringLiteral("#E4E4E7");
        hovFg = QStringLiteral("#18181B");
        break;
    }
    return QStringLiteral(
               "QTreeWidget#%1 {\n"
               "  background: %2;\n"
               "  border: 1px solid %3;\n"
               "  border-radius: 12px;\n"
               "  outline: none;\n"
               "  font-size: 13px;\n"
               "  color: %4;\n"
               "  padding: 8px 4px;\n"
               "  show-decoration-selected: 0;\n"
               "}\n"
               "QTreeWidget#%1:focus {\n"
               "  outline: none;\n"
               "}\n"
               "QTreeWidget#%1::item {\n"
               "  padding: 8px 12px;\n"
               "  border-radius: 8px;\n"
               "  min-height: 22px;\n"
               "  outline: none;\n"
               "}\n"
               "QTreeWidget#%1::item:selected {\n"
               "  background: %5;\n"
               "  color: %6;\n"
               "  border: none;\n"
               "}\n"
               "QTreeWidget#%1::item:hover:!selected {\n"
               "  background: %7;\n"
               "  color: %8;\n"
               "}\n"
               "QTreeWidget#%1::item:focus {\n"
               "  outline: none;\n"
               "  border: none;\n"
               "}\n")
        .arg(treeObjectName, bg, border, text, selBg, selFg, hovBg, hovFg);
}

QString ApplyStyle::helpDialogStyle(MainWindowTheme theme)
{
    theme = MainWindowTheme::Default;
    QString core;
    switch (theme) {
    case MainWindowTheme::Cool:
        core = QStringLiteral(R"QSS(
            HelpDialog {
                background: #0b1220;
            }
            QSplitter {
                background: transparent;
            }
            QSplitter::handle {
                background: #334155;
                width: 2px;
                border-radius: 1px;
            }

            )QSS")
                  + sidebarTocTreeStyleSheet(QStringLiteral("helpToc"), MainWindowTheme::Cool)
                  + QStringLiteral(R"QSS(

            #helpBrowser {
                background: #1e293b;
                border: 1px solid #334155;
                border-radius: 12px;
                padding: 20px 24px;
                font-size: 13px;
                color: #e2e8f0;
                selection-background-color: #38bdf8;
                selection-color: #0f172a;
            }
        )QSS");
        break;
    case MainWindowTheme::Warm:
        core = QStringLiteral(R"QSS(
            HelpDialog {
                background: #352f2c;
            }
            QSplitter {
                background: transparent;
            }
            QSplitter::handle {
                background: #7a5b43;
                width: 2px;
                border-radius: 1px;
            }

            )QSS")
                  + sidebarTocTreeStyleSheet(QStringLiteral("helpToc"), MainWindowTheme::Warm)
                  + QStringLiteral(R"QSS(

            #helpBrowser {
                background: #fffcf9;
                border: 1px solid #d6cbc0;
                border-radius: 12px;
                padding: 20px 24px;
                font-size: 13px;
                color: #3d3429;
                selection-background-color: #fde68a;
                selection-color: #3d3429;
            }
        )QSS");
        break;
    case MainWindowTheme::Default:
    default:
        core = QStringLiteral(R"QSS(
            HelpDialog {
                background: #E4E4E7;
            }
            QSplitter {
                background: transparent;
            }
            QSplitter::handle {
                background: #D4D4D8;
                width: 2px;
                border-radius: 1px;
            }

            )QSS")
                  + sidebarTocTreeStyleSheet(QStringLiteral("helpToc"), MainWindowTheme::Default)
                  + QStringLiteral(R"QSS(

            #helpBrowser {
                background: #FFFFFF;
                border: 1px solid #D4D4D8;
                border-radius: 12px;
                padding: 20px 24px;
                font-size: 13px;
                color: #18181B;
                selection-background-color: #E4E4E7;
                selection-color: #18181B;
            }
        )QSS");
        break;
    }
    return core + unifiedScrollBarQss(theme);
}

namespace {

struct SimpleManagerDialogTokens {
    const char* dialogBg;
    const char* dialogText;
    const char* panelBg;
    const char* panelBorder;
    const char* listHoverBg;
    const char* listSelectedBg;
    const char* listSelectedText;
    const char* inputBg;
    const char* inputBorder;
    const char* inputText;
    const char* accent;
    const char* accentHover;
    const char* accentPressed;
    const char* treeHeaderBg;
    const char* treeAlternateBg;
};

QString buildStatusMessageManagerQss(const SimpleManagerDialogTokens& t)
{
    return QStringLiteral(
        R"QSS(
        QDialog#statusMessageManagerDialog {
            background: %1;
        }
        QDialog#statusMessageManagerDialog QLabel {
            color: %2;
        }
        QListWidget#statusMessageList {
            background: %3;
            border: 1px solid %4;
            border-radius: 10px;
            color: %2;
        }
        QListWidget#statusMessageList::item {
            padding: 6px 10px;
            color: %2;
        }
        QListWidget#statusMessageList::item:hover {
            background: %5;
        }
        QListWidget#statusMessageList::item:selected {
            background: %6;
            color: %7;
        }

        QLineEdit#statusMessageEditor {
            background: %8;
            border: 1px solid %9;
            border-radius: 10px;
            padding: 8px 12px;
            font-size: 13px;
            color: %10;
        }
        QLineEdit#statusMessageEditor:focus {
            border-color: %11;
        }

        QPushButton#statusMessageAddButton,
        QPushButton#statusMessageUpdateButton,
        QPushButton#statusMessageDeleteButton,
        QPushButton#statusMessageCloseButton {
            background: %11;
            color: #ffffff;
            border: none;
            border-radius: 10px;
            padding: 6px 14px;
            font-size: 13px;
            font-weight: 600;
        }
        QPushButton#statusMessageAddButton:hover,
        QPushButton#statusMessageUpdateButton:hover,
        QPushButton#statusMessageDeleteButton:hover,
        QPushButton#statusMessageCloseButton:hover {
            background: %12;
        }
        QPushButton#statusMessageAddButton:pressed,
        QPushButton#statusMessageUpdateButton:pressed,
        QPushButton#statusMessageDeleteButton:pressed,
        QPushButton#statusMessageCloseButton:pressed {
            background: %13;
        }
    )QSS")
        .arg(QLatin1String(t.dialogBg))
        .arg(QLatin1String(t.dialogText))
        .arg(QLatin1String(t.panelBg))
        .arg(QLatin1String(t.panelBorder))
        .arg(QLatin1String(t.listHoverBg))
        .arg(QLatin1String(t.listSelectedBg))
        .arg(QLatin1String(t.listSelectedText))
        .arg(QLatin1String(t.inputBg))
        .arg(QLatin1String(t.inputBorder))
        .arg(QLatin1String(t.inputText))
        .arg(QLatin1String(t.accent))
        .arg(QLatin1String(t.accentHover))
        .arg(QLatin1String(t.accentPressed));
}

QString buildQuickLaunchManagerQss(const SimpleManagerDialogTokens& t)
{
    return QStringLiteral(
        R"QSS(
        QDialog#quickLaunchManagerDialog,
        QDialog#quickLaunchRulesDialog {
            background: %1;
        }
        QDialog#quickLaunchManagerDialog QLabel,
        QDialog#quickLaunchRulesDialog QLabel,
        QDialog#quickLaunchManagerDialog QListWidget,
        QDialog#quickLaunchManagerDialog QTreeWidget,
        QDialog#quickLaunchRulesDialog QTreeWidget,
        QDialog#quickLaunchManagerDialog QCheckBox,
        QDialog#quickLaunchRulesDialog QCheckBox,
        QDialog#quickLaunchManagerDialog QToolButton,
        QDialog#quickLaunchRulesDialog QToolButton,
        QDialog#quickLaunchRulesDialog QComboBox,
        QDialog#quickLaunchRulesDialog QGroupBox,
        QDialog#quickLaunchRulesDialog QLineEdit {
            color: %2;
        }
        QLineEdit#quickLaunchRulesSearchEdit {
            background: %3;
            border: 1px solid %4;
            border-radius: 8px;
            padding: 6px 10px;
            font-size: 13px;
        }

        QDialog#quickLaunchRulesDialog QComboBox {
            color: %2;
            background: %3;
            border: 1px solid %4;
            border-radius: 8px;
            padding: 4px 10px 4px 10px;
            min-height: 28px;
            font-size: 13px;
        }
        QDialog#quickLaunchRulesDialog QComboBox:hover {
            border-color: %11;
        }
        QDialog#quickLaunchRulesDialog QComboBox:focus {
            border-color: %8;
        }
        QDialog#quickLaunchRulesDialog QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 28px;
            border: none;
            border-left: 1px solid %4;
            border-top-right-radius: 7px;
            border-bottom-right-radius: 7px;
            background: transparent;
        }
        QDialog#quickLaunchRulesDialog QComboBox::down-arrow {
            image: url(:/fold_arrow_to_expand_icon.svg);
            width: 16px;
            height: 16px;
        }
        QDialog#quickLaunchRulesDialog QComboBox[popupOpen="true"]::down-arrow {
            image: url(:/fold_arrow_to_collapse_icon.svg);
        }
        QDialog#quickLaunchRulesDialog QComboBox QAbstractItemView {
            background: %3;
            color: %2;
            selection-background-color: %6;
            selection-color: %7;
            border: 1px solid %4;
            outline: none;
            padding: 2px;
        }

        QListWidget#quickLaunchAppList {
            background: %3;
            border: 1px solid %4;
            border-radius: 10px;
            color: %2;
        }
        QListWidget#quickLaunchAppList::item {
            padding: 6px 10px;
            color: %2;
        }
        QListWidget#quickLaunchAppList::item:hover {
            background: %5;
        }
        QListWidget#quickLaunchAppList::item:selected {
            background: %6;
            color: %7;
        }

        QTreeWidget#quickLaunchAppTree,
        QTreeWidget#quickLaunchRulesTree {
            background: %3;
            alternate-background-color: %12;
            border: 1px solid %4;
            border-radius: 10px;
            color: %2;
            outline: none;
            show-decoration-selected: 0;
        }
        QTreeWidget#quickLaunchAppTree::item,
        QTreeWidget#quickLaunchRulesTree::item {
            padding: 5px 8px;
            color: %2;
            border: none;
            outline: none;
        }
        QTreeWidget#quickLaunchAppTree::item:hover,
        QTreeWidget#quickLaunchRulesTree::item:hover {
            background: %5;
        }
        QTreeWidget#quickLaunchAppTree::item:selected,
        QTreeWidget#quickLaunchRulesTree::item:selected {
            background: %6;
            color: %7;
        }
        QTreeWidget#quickLaunchAppTree::branch,
        QTreeWidget#quickLaunchRulesTree::branch {
            background: transparent;
        }
        QTreeWidget#quickLaunchAppTree::branch:has-children:!has-siblings:closed,
        QTreeWidget#quickLaunchAppTree::branch:closed:has-children:has-siblings,
        QTreeWidget#quickLaunchRulesTree::branch:has-children:!has-siblings:closed,
        QTreeWidget#quickLaunchRulesTree::branch:closed:has-children:has-siblings {
            border-image: none;
            image: url(:/fold_arrow_to_expand_icon.svg);
        }
        QTreeWidget#quickLaunchAppTree::branch:has-children:!has-siblings:open,
        QTreeWidget#quickLaunchAppTree::branch:open:has-children:has-siblings,
        QTreeWidget#quickLaunchRulesTree::branch:has-children:!has-siblings:open,
        QTreeWidget#quickLaunchRulesTree::branch:open:has-children:has-siblings {
            border-image: none;
            image: url(:/fold_arrow_to_collapse_icon.svg);
        }
        QTreeWidget#quickLaunchAppTree QHeaderView::section,
        QTreeWidget#quickLaunchRulesTree QHeaderView::section {
            background: %11;
            color: %2;
            border: none;
            border-bottom: 1px solid %4;
            border-right: 1px solid %4;
            padding: 8px 10px;
            font-weight: 600;
            font-size: 12px;
        }
        QTreeWidget#quickLaunchAppTree QHeaderView::section:first,
        QTreeWidget#quickLaunchRulesTree QHeaderView::section:first {
            border-top-left-radius: 9px;
        }
        QTreeWidget#quickLaunchAppTree QHeaderView::section:last,
        QTreeWidget#quickLaunchRulesTree QHeaderView::section:last {
            border-right: none;
            border-top-right-radius: 9px;
        }

        QCheckBox#quickLaunchOnlyBox {
            spacing: 8px;
            font-size: 13px;
        }

        QPushButton#quickLaunchAddButton,
        QPushButton#quickLaunchAddGroupButton,
        QPushButton#quickLaunchChangeTargetButton,
        QPushButton#quickLaunchScanRulesButton,
        QPushButton#quickLaunchDeleteCheckedButton,
        QPushButton#quickLaunchDeleteAllButton,
        QPushButton#quickLaunchAutoScanButton,
        QPushButton#quickLaunchRulesActionButton,
        QPushButton#quickLaunchOkButton,
        QPushButton#quickLaunchCancelButton {
            background: %8;
            color: #ffffff;
            border: none;
            border-radius: 10px;
            padding: 6px 14px;
            font-size: 13px;
            font-weight: 600;
        }
        QPushButton#quickLaunchAddButton:hover,
        QPushButton#quickLaunchAddGroupButton:hover,
        QPushButton#quickLaunchChangeTargetButton:hover,
        QPushButton#quickLaunchScanRulesButton:hover,
        QPushButton#quickLaunchDeleteCheckedButton:hover,
        QPushButton#quickLaunchDeleteAllButton:hover,
        QPushButton#quickLaunchAutoScanButton:hover,
        QPushButton#quickLaunchRulesActionButton:hover,
        QPushButton#quickLaunchOkButton:hover,
        QPushButton#quickLaunchCancelButton:hover {
            background: %9;
        }
        QPushButton#quickLaunchAddButton:pressed,
        QPushButton#quickLaunchAddGroupButton:pressed,
        QPushButton#quickLaunchChangeTargetButton:pressed,
        QPushButton#quickLaunchScanRulesButton:pressed,
        QPushButton#quickLaunchDeleteCheckedButton:pressed,
        QPushButton#quickLaunchDeleteAllButton:pressed,
        QPushButton#quickLaunchAutoScanButton:pressed,
        QPushButton#quickLaunchRulesActionButton:pressed,
        QPushButton#quickLaunchOkButton:pressed,
        QPushButton#quickLaunchCancelButton:pressed {
            background: %10;
        }
    )QSS")
        .arg(QLatin1String(t.dialogBg))
        .arg(QLatin1String(t.dialogText))
        .arg(QLatin1String(t.panelBg))
        .arg(QLatin1String(t.panelBorder))
        .arg(QLatin1String(t.listHoverBg))
        .arg(QLatin1String(t.listSelectedBg))
        .arg(QLatin1String(t.listSelectedText))
        .arg(QLatin1String(t.accent))
        .arg(QLatin1String(t.accentHover))
        .arg(QLatin1String(t.accentPressed))
        .arg(QLatin1String(t.treeHeaderBg))
        .arg(QLatin1String(t.treeAlternateBg));
}

QString quickLaunchManagerCheckboxQss(ApplyStyle::MainWindowTheme theme)
{
    switch (theme) {
    case ApplyStyle::MainWindowTheme::Cool:
        return QStringLiteral(
            R"(
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 3px;
        }
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator:unchecked {
            background-color: #0b1220;
            border: 1px solid #334155;
        }
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator:unchecked:hover {
            border-color: #475569;
        }
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator:checked {
            image: url(:/add_window_checkbox_checked_cool.svg);
            border: none;
        }
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator:checked:hover {
            image: url(:/add_window_checkbox_checked_cool_hover.svg);
        }
    )");
    case ApplyStyle::MainWindowTheme::Warm:
        return QStringLiteral(
            R"(
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 3px;
        }
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator:unchecked {
            background-color: #2a2624;
            border: 1px solid #7a5b43;
        }
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator:unchecked:hover {
            border-color: #a8927a;
        }
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator:checked {
            image: url(:/add_window_checkbox_checked_warm.svg);
            border: none;
        }
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator:checked:hover {
            image: url(:/add_window_checkbox_checked_warm_hover.svg);
        }
    )");
    case ApplyStyle::MainWindowTheme::Default:
    default:
        return QStringLiteral(
            R"(
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 3px;
        }
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator:unchecked {
            background-color: #FFFFFF;
            border: 2px solid #A1A1AA;
        }
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator:unchecked:hover {
            border-color: #71717A;
        }
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator:checked {
            image: url(:/add_window_checkbox_checked.svg);
            border: none;
        }
        QDialog#quickLaunchManagerDialog QCheckBox#quickLaunchOnlyBox::indicator:checked:hover {
            image: url(:/add_window_checkbox_checked_hover.svg);
        }
    )");
    }
}

/** 列表内 QTreeWidgetItem 复选框（滚动条由 unifiedScrollBarQss 统一注入管理类对话框） */
QString quickLaunchManagerTreeIndicatorAndScrollQss(ApplyStyle::MainWindowTheme theme)
{
    switch (theme) {
    case ApplyStyle::MainWindowTheme::Cool:
        return QStringLiteral(
            R"(
        QTreeWidget#quickLaunchAppTree::indicator,
        QTreeWidget#quickLaunchRulesTree::indicator {
            width: 17px;
            height: 17px;
            border-radius: 3px;
        }
        QTreeWidget#quickLaunchAppTree::indicator:unchecked,
        QTreeWidget#quickLaunchRulesTree::indicator:unchecked {
            background-color: #0b1220;
            border: 2px solid #475569;
        }
        QTreeWidget#quickLaunchAppTree::indicator:unchecked:hover,
        QTreeWidget#quickLaunchRulesTree::indicator:unchecked:hover {
            border-color: #94a3b8;
        }
        QTreeWidget#quickLaunchAppTree::indicator:checked,
        QTreeWidget#quickLaunchRulesTree::indicator:checked {
            image: url(:/add_window_checkbox_checked_cool.svg);
            border: none;
        }
        QTreeWidget#quickLaunchAppTree::indicator:checked:hover,
        QTreeWidget#quickLaunchRulesTree::indicator:checked:hover {
            image: url(:/add_window_checkbox_checked_cool_hover.svg);
        }
    )");
    case ApplyStyle::MainWindowTheme::Warm:
        return QStringLiteral(
            R"(
        QTreeWidget#quickLaunchAppTree::indicator,
        QTreeWidget#quickLaunchRulesTree::indicator {
            width: 17px;
            height: 17px;
            border-radius: 3px;
        }
        QTreeWidget#quickLaunchAppTree::indicator:unchecked,
        QTreeWidget#quickLaunchRulesTree::indicator:unchecked {
            background-color: #2a2624;
            border: 2px solid #a8927a;
        }
        QTreeWidget#quickLaunchAppTree::indicator:unchecked:hover,
        QTreeWidget#quickLaunchRulesTree::indicator:unchecked:hover {
            border-color: #d6cbc0;
        }
        QTreeWidget#quickLaunchAppTree::indicator:checked,
        QTreeWidget#quickLaunchRulesTree::indicator:checked {
            image: url(:/add_window_checkbox_checked_warm.svg);
            border: none;
        }
        QTreeWidget#quickLaunchAppTree::indicator:checked:hover,
        QTreeWidget#quickLaunchRulesTree::indicator:checked:hover {
            image: url(:/add_window_checkbox_checked_warm_hover.svg);
        }
    )");
    case ApplyStyle::MainWindowTheme::Default:
    default:
        return QStringLiteral(
            R"(
        QTreeWidget#quickLaunchAppTree::indicator,
        QTreeWidget#quickLaunchRulesTree::indicator {
            width: 17px;
            height: 17px;
            border-radius: 3px;
        }
        QTreeWidget#quickLaunchAppTree::indicator:unchecked,
        QTreeWidget#quickLaunchRulesTree::indicator:unchecked {
            background-color: #FFFFFF;
            border: 2px solid #BFDBFE;
        }
        QTreeWidget#quickLaunchAppTree::indicator:unchecked:hover,
        QTreeWidget#quickLaunchRulesTree::indicator:unchecked:hover {
            border-color: #93C5FD;
        }
        QTreeWidget#quickLaunchAppTree::indicator:checked,
        QTreeWidget#quickLaunchRulesTree::indicator:checked {
            image: url(:/add_window_checkbox_checked.svg);
            border: none;
        }
        QTreeWidget#quickLaunchAppTree::indicator:checked:hover,
        QTreeWidget#quickLaunchRulesTree::indicator:checked:hover {
            image: url(:/add_window_checkbox_checked_hover.svg);
        }
    )");
    }
}

const SimpleManagerDialogTokens kStatusDefault = {
    "#EDF4FF",
    "#0F172A",
    "#FFFFFF",
    "#BFDBFE",
    "rgba(0,178,255,0.10)",
    "#DBEAFE",
    "#0F172A",
    "#FFFFFF",
    "#BFDBFE",
    "#0F172A",
    "#00B2FF",
    "#33C4FF",
    "#0090DD",
    "#EFF6FF",
    "#F8FAFF",
};

const SimpleManagerDialogTokens kStatusCool = {
    "#0f172a",
    "#e2e8f0",
    "#0b1220",
    "#334155",
    "rgba(14,165,233,0.14)",
    "#0284c7",
    "#ffffff",
    "#0b1220",
    "#334155",
    "#e2e8f0",
    "#0284c7",
    "#0ea5e9",
    "#0369a1",
    "#1e293b",
    "#0f172a",
};

const SimpleManagerDialogTokens kStatusWarm = {
    "#3d3835",
    "#fff7ed",
    "#2a2624",
    "#7a5b43",
    "rgba(180,83,9,0.12)",
    "#b45309",
    "#ffffff",
    "#2a2624",
    "#7a5b43",
    "#fff7ed",
    "#b45309",
    "#d97706",
    "#7c2d12",
    "#3d3835",
    "#352f2c",
};

} // namespace

QString ApplyStyle::statusMessageManagerStyle(MainWindowTheme theme)
{
    Q_UNUSED(theme)
    return buildStatusMessageManagerQss(kStatusDefault)
        + baseControlStyleQss()
        + unifiedScrollBarQss(MainWindowTheme::Default);
}

QString ApplyStyle::quickLaunchManagerStyle(MainWindowTheme theme)
{
    Q_UNUSED(theme)
    return buildQuickLaunchManagerQss(kStatusDefault)
        + baseControlStyleQss()
        + quickLaunchManagerCheckboxQss(MainWindowTheme::Default)
        + quickLaunchManagerTreeIndicatorAndScrollQss(MainWindowTheme::Default)
        + unifiedScrollBarQss(MainWindowTheme::Default);
}
