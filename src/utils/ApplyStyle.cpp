#include "applystyle.h"

#include <QSettings>

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
 * Default = 经典黑灰白（化繁为简）：
 * 深色侧栏锚定导航、浅色工作区承载内容（常见控制台布局，易读、层次清）。
 * 避免整屏同一明度灰；大面用 #F4F4F5 / #FFFFFF，线框用 #E4E4E7，正文 #18181B，弱文案 #71717A。
 */
const MainWindowStyleTokens kTokensDefault = {
    "#F4F4F5", /* rootBg */
    "#F4F4F5", /* rightAreaBg */
    "#27272A", /* leftSidebarBg — 外圈深灰 */
    "#FFFFFF", /* topBarBg */
    "#E4E4E7", /* topBarBorderBottom */
    "#18181B", /* topTitleColor */
    "#FFFFFF", /* readyWrapBg */
    "#E4E4E7", /* readyWrapBorder */
    "#18181B", /* readyTextColor */
    "#F4F4F5", /* centerBg */
    "#FFFFFF", /* cardBg */
    "#E4E4E7", /* cardBorder */
    "#F4F4F5", /* rocketWrapBg */
    "#18181B", /* readyTitleColor */
    "#E4E4E7", /* dividerBg */
    "#71717A", /* mutedTextColor */
    "#FFFFFF", /* quickCardBg */
    "#E4E4E7", /* quickCardBorder */
    "#18181B", /* quickCardText */
    "#F4F4F5", /* quickCardHoverBg */
    "#A1A1AA", /* quickCardHoverBorder */
    "#E4E4E7", /* quickCardPressedBg */
    "#27272A", /* statusBarBg — 与侧栏一致 */
    "#FFFFFF", /* statusBarText */
    "#3F3F46", /* statusBarBorderTop */
    "#FFFFFF", /* statusLabelColor — 格言、时间、分隔符纯白 */
    "#FFFFFF", /* themeButtonColor */
    "#A1A1AA", /* themeButtonBorder */
    "rgba(255,255,255,0.08)", /* themeButtonHoverBg */
    "#27272A", /* platformTreePanelBg — 与侧栏同深灰，仅行块为浅色 */
    "#3F3F46", /* platformTreePanelBorder — 略浅一线，框出树区域 */
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
        QWidget#leftSidebar { background: %3; }
        QTreeView#platformList {
            background: %30;
            border: 1px solid %31;
            border-radius: 10px;
            outline: none;
        }
        QTreeView#platformList::item { background: transparent; }

        QWidget#userProfileBar {
            border-radius: 8px;
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
            border-radius: 8px;
        }

        QWidget#topBar {
            background: %4;
            border-bottom: 1px solid %5;
        }
        QLabel#topTitle {
            color: %6;
            font-size: 14px;
            font-weight: 600;
        }
        QWidget#readyWrap {
            background: %7;
            border: 1px solid %8;
            border-radius: 12px;
        }
        QLabel#readyText {
            color: %9;
            font-size: 16px;
            font-weight: 600;
        }

        QWidget#centerArea, QWidget#centerStack, QWidget#placeholderPage {
            background: %10;
        }

        QFrame#readyCard, QFrame#platformCard {
            background: %11;
            border-radius: 14px;
            border: 1px solid %12;
        }
        QFrame#rocketWrap {
            background: %13;
            border-radius: 20px;
        }

        QLabel#readyTitle {
            color: %14;
            font-size: 22px;
            font-weight: 700;
        }
        QFrame#divider {
            background: %15;
        }
        QLabel#readySubtitle {
            color: %16;
            font-size: 13px;
        }
        QLabel#placeholderText {
            color: %16;
            font-size: 14px;
        }

        QToolButton#quickCard {
            background: %17;
            border: 1px solid %18;
            border-radius: 12px;
            padding: 10px;
            color: %19;
            font-size: 13px;
        }
        QToolButton#quickCard:hover {
            background: %20;
            border-color: %21;
        }
        QToolButton#quickCard:pressed {
            background: %22;
        }

        QStatusBar {
            background: %23;
            color: %24;
            font-size: 12px;
            border-top: 1px solid %25;
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

} // namespace

/** 与「管理应用列表」内树视图一致的细滚动条（无箭头、圆角滑块） */
static QString unifiedScrollBarQss(ApplyStyle::MainWindowTheme theme)
{
    switch (theme) {
    case ApplyStyle::MainWindowTheme::Cool:
        return QStringLiteral(
            R"(
        QScrollBar:vertical {
            width: 6px;
            background: transparent;
            margin: 2px 3px 2px 0;
        }
        QScrollBar::handle:vertical {
            background: #475569;
            min-height: 28px;
            border-radius: 3px;
        }
        QScrollBar::handle:vertical:hover {
            background: #64748b;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
            width: 0;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollBar:horizontal {
            height: 6px;
            background: transparent;
            margin: 0 0 3px 0;
        }
        QScrollBar::handle:horizontal {
            background: #475569;
            min-width: 28px;
            border-radius: 3px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #64748b;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            height: 0;
            width: 0;
        }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: transparent;
        }
    )");
    case ApplyStyle::MainWindowTheme::Warm:
        return QStringLiteral(
            R"(
        QScrollBar:vertical {
            width: 6px;
            background: transparent;
            margin: 2px 3px 2px 0;
        }
        QScrollBar::handle:vertical {
            background: #a8927a;
            min-height: 28px;
            border-radius: 3px;
        }
        QScrollBar::handle:vertical:hover {
            background: #7a5b43;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
            width: 0;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollBar:horizontal {
            height: 6px;
            background: transparent;
            margin: 0 0 3px 0;
        }
        QScrollBar::handle:horizontal {
            background: #a8927a;
            min-width: 28px;
            border-radius: 3px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #7a5b43;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            height: 0;
            width: 0;
        }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: transparent;
        }
    )");
    case ApplyStyle::MainWindowTheme::Default:
    default:
        return QStringLiteral(
            R"(
        QScrollBar:vertical {
            width: 6px;
            background: transparent;
            margin: 2px 3px 2px 0;
        }
        QScrollBar::handle:vertical {
            background: #C4C4CC;
            min-height: 28px;
            border-radius: 3px;
        }
        QScrollBar::handle:vertical:hover {
            background: #A1A1AA;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
            width: 0;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollBar:horizontal {
            height: 6px;
            background: transparent;
            margin: 0 0 3px 0;
        }
        QScrollBar::handle:horizontal {
            background: #C4C4CC;
            min-width: 28px;
            border-radius: 3px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #A1A1AA;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            height: 0;
            width: 0;
        }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: transparent;
        }
    )");
    }
}

/** 「添加新窗口 / 控制台输出」等 QDialog 内 QComboBox，与聚合接待模式下拉一致 */
static QString addWindowDialogComboBoxQss(ApplyStyle::MainWindowTheme theme)
{
    switch (theme) {
    case ApplyStyle::MainWindowTheme::Cool:
        return QStringLiteral(
            R"QSS(
        QDialog QComboBox {
            color: #1e293b;
            background-color: #ffffff;
            border: 1px solid #c0d9f7;
            border-radius: 8px;
            padding: 4px 10px;
            min-height: 24px;
            font-size: 12px;
        }
        QDialog QComboBox:hover {
            border-color: #8eb4e0;
        }
        QDialog QComboBox:focus {
            border-color: #60a5fa;
        }
        QDialog QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 26px;
            border: none;
            border-left: 1px solid #c0d9f7;
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
            background: #ffffff;
            color: #1e293b;
            selection-background-color: #b8d4f5;
            selection-color: #1e293b;
            border: 1px solid #c0d9f7;
            outline: none;
            padding: 2px;
        }
    )QSS");
    case ApplyStyle::MainWindowTheme::Warm:
        return QStringLiteral(
            R"QSS(
        QDialog QComboBox {
            color: #3d3429;
            background-color: #fffcf9;
            border: 1px solid #e5ddd4;
            border-radius: 8px;
            padding: 4px 10px;
            min-height: 24px;
            font-size: 12px;
        }
        QDialog QComboBox:hover {
            border-color: #d6cbc0;
        }
        QDialog QComboBox:focus {
            border-color: #b45309;
        }
        QDialog QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 26px;
            border: none;
            border-left: 1px solid #e5ddd4;
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
            background: #fffcf9;
            color: #3d3429;
            selection-background-color: #fef3c7;
            selection-color: #3d3429;
            border: 1px solid #e5ddd4;
            outline: none;
            padding: 2px;
        }
    )QSS");
    case ApplyStyle::MainWindowTheme::Default:
    default:
        return QStringLiteral(
            R"QSS(
        QDialog QComboBox {
            color: #18181B;
            background-color: #FFFFFF;
            border: 1px solid #D4D4D8;
            border-radius: 8px;
            padding: 4px 10px;
            min-height: 24px;
            font-size: 12px;
        }
        QDialog QComboBox:hover {
            border-color: #A1A1AA;
        }
        QDialog QComboBox:focus {
            border-color: #71717A;
        }
        QDialog QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 26px;
            border: none;
            border-left: 1px solid #D4D4D8;
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
            selection-background-color: #D4D4D8;
            selection-color: #18181B;
            border: 1px solid #D4D4D8;
            outline: none;
            padding: 2px;
        }
    )QSS");
    }
}

PlatformTreeColors ApplyStyle::platformTreeColors(MainWindowTheme theme)
{
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
        /* 树区域为浅底（QSS），行块为浅卡片 + 深色字，未关联客服项仍可辨 */
        c.groupBgDefault = QColor(0xfa, 0xfa, 0xfa);
        c.groupBgHover = QColor(0xff, 0xff, 0xff);
        c.groupBgSelected = QColor(0xe4, 0xe4, 0xe7);
        c.groupTextColor = QColor(0x18, 0x18, 0x1b);
        c.groupArrowColor = QColor(0x71, 0x71, 0x7a);
        c.itemBgDefault = QColor(0xfa, 0xfa, 0xfa);
        c.itemBgHover = QColor(0xff, 0xff, 0xff);
        c.itemBgSelected = QColor(0xe4, 0xe4, 0xe7);
        c.itemTextColor = QColor(0x18, 0x18, 0x1b);
        c.itemAccentBarColor = QColor(0x3f, 0x3f, 0x46);
        c.itemInactiveBgDefault = QColor(0xf4, 0xf4, 0xf5);
        c.itemInactiveBgHover = QColor(0xe4, 0xe4, 0xe7);
        c.itemInactiveBgSelected = QColor(0xd4, 0xd4, 0xd8);
        c.itemInactiveTextColor = QColor(0x52, 0x52, 0x5b);
        c.csDotActivated = QColor(0x22, 0xc5, 0x5e);
        c.csDotInactive = QColor(0xa1, 0xa1, 0xaa);
        c.itemInactiveIconOpacity = 0.55;
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

QString ApplyStyle::mainWindowStyle(MainWindowTheme theme)
{
    switch (theme) {
    case MainWindowTheme::Cool:
        return buildMainWindowStyleQss(kTokensCool) + unifiedScrollBarQss(MainWindowTheme::Cool);
    case MainWindowTheme::Warm:
        return buildMainWindowStyleQss(kTokensWarm) + unifiedScrollBarQss(MainWindowTheme::Warm);
    case MainWindowTheme::Default:
    default:
        return buildMainWindowStyleQss(kTokensDefault) + unifiedScrollBarQss(MainWindowTheme::Default);
    }
}

QString ApplyStyle::mainWindowStyle()
{
    return mainWindowStyle(MainWindowTheme::Default);
}

ApplyStyle::MainWindowTheme ApplyStyle::loadSavedMainWindowTheme()
{
    QSettings settings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    const int v = settings.value(QStringLiteral("ui/main_window_theme"), 0).toInt();
    if (v == static_cast<int>(MainWindowTheme::Cool))
        return MainWindowTheme::Cool;
    if (v == static_cast<int>(MainWindowTheme::Warm))
        return MainWindowTheme::Warm;
    return MainWindowTheme::Default;
}

void ApplyStyle::saveMainWindowTheme(MainWindowTheme theme)
{
    QSettings settings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    settings.setValue(QStringLiteral("ui/main_window_theme"), static_cast<int>(theme));
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

/** Default：白底弹窗 + 细线分割，与主窗「深侧栏+浅内容」一致 */
static QString addWindowDialogStyleSkyQss()
{
    return QStringLiteral(R"QSS(
        QDialog {
            background-color: #FFFFFF;
        }
        QDialog QLabel {
            color: #18181B;
            background: transparent;
            font-size: 13px;
        }
        QDialog QLineEdit {
            color: #18181B;
            background-color: #FFFFFF;
            border: 1px solid #D4D4D8;
            border-radius: 8px;
            padding: 6px 10px;
            font-size: 13px;
            selection-background-color: #D4D4D8;
            selection-color: #18181B;
        }
        QDialog QLineEdit:focus {
            border-color: #71717A;
        }
        QDialog QLineEdit:read-only {
            background-color: #F4F4F5;
            color: #71717A;
        }
        QDialog QPushButton {
            color: #18181B;
            background-color: #FFFFFF;
            border: 1px solid #D4D4D8;
            border-radius: 8px;
            padding: 6px 16px;
            font-size: 13px;
            min-height: 22px;
        }
        QDialog QPushButton:hover {
            background-color: #F4F4F5;
            border-color: #A1A1AA;
        }
        QDialog QPushButton:focus {
            background-color: #F4F4F5;
            border: 2px solid #C4C4CC;
            color: #18181B;
        }
        QDialog QPushButton:hover:focus {
            background-color: #E4E4E7;
            border-color: #A1A1AA;
        }
        QDialog QPushButton:pressed {
            background-color: #E4E4E7;
            border-color: #71717A;
        }
        QDialog QPushButton:pressed:focus {
            border-color: #71717A;
        }
        QDialog QPushButton:default {
            color: #FFFFFF;
            background-color: #71717A;
            border: 1px solid #71717A;
            font-weight: 600;
        }
        QDialog QPushButton:default:hover {
            background-color: #52525B;
            border-color: #52525B;
            color: #FFFFFF;
        }
        QDialog QPushButton:default:focus {
            border: 2px solid #A1A1AA;
            background-color: #71717A;
            color: #FFFFFF;
        }
        QDialog QPushButton:default:hover:focus {
            background-color: #52525B;
            border-color: #A1A1AA;
            color: #FFFFFF;
        }
        QDialog QPushButton:default:pressed {
            background-color: #52525B;
            border-color: #3F3F46;
            color: #FFFFFF;
        }
        QDialog QPushButton:default:pressed:focus {
            border-color: #3F3F46;
        }
        QDialog QTableWidget {
            background-color: #FFFFFF;
            alternate-background-color: #F4F4F5;
            color: #18181B;
            gridline-color: #E4E4E7;
            border: 1px solid #E4E4E7;
            border-radius: 10px;
            font-size: 13px;
            selection-background-color: #D4D4D8;
            selection-color: #18181B;
        }
        QDialog QTableWidget:focus {
            outline: none;
        }
        QDialog QTableWidget::item {
            color: #18181B;
            padding: 4px;
        }
        QDialog QTableWidget::item:hover {
            background-color: #E4E4E7;
            color: #18181B;
        }
        QDialog QTableWidget::item:selected {
            background-color: #D4D4D8;
            color: #18181B;
        }
        QDialog QTableWidget::item:selected:hover {
            background-color: #C4C4CC;
            color: #18181B;
        }
        QDialog QTableWidget QTableCornerButton::section {
            background-color: #F4F4F5;
            border: 1px solid #E4E4E7;
        }
        QDialog QTableWidget QHeaderView {
            background-color: #F4F4F5;
        }
        QDialog QTableWidget QHeaderView::section:horizontal {
            background-color: #F4F4F5;
            color: #18181B;
            padding: 8px;
            border: none;
            border-bottom: 1px solid #E4E4E7;
            border-right: 1px solid #E4E4E7;
            font-size: 12px;
            font-weight: 600;
        }
        QDialog QTableWidget QHeaderView::section:vertical {
            background-color: #FAFAFA;
            color: #52525B;
            padding: 4px;
            border: none;
            border-right: 1px solid #E4E4E7;
            border-bottom: 1px solid #E4E4E7;
            font-size: 12px;
        }
        QDialog QCheckBox {
            color: #18181B;
            spacing: 6px;
        }
        QDialog QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 3px;
        }
        QDialog QCheckBox::indicator:unchecked {
            background-color: #FFFFFF;
            border: 1px solid #D4D4D8;
        }
        QDialog QCheckBox::indicator:unchecked:hover {
            border-color: #A1A1AA;
        }
        QDialog QCheckBox::indicator:checked {
            image: url(:/add_window_checkbox_checked.svg);
            border: none;
        }
        QDialog QCheckBox::indicator:checked:hover {
            image: url(:/add_window_checkbox_checked_hover.svg);
        }
        QDialog QCheckBox::indicator:disabled {
            background-color: #F4F4F5;
            border-color: #E4E4E7;
        }
    )QSS");
}

QString ApplyStyle::addWindowDialogStyle(MainWindowTheme theme)
{
    QString base;
    switch (theme) {
    case MainWindowTheme::Cool:
        base = addWindowDialogStyleLegacyQss();
        break;
    case MainWindowTheme::Warm:
        base = addWindowDialogStyleWarmQss();
        break;
    case MainWindowTheme::Default:
    default:
        base = addWindowDialogStyleSkyQss();
        break;
    }
    return base + addWindowDialogComboBoxQss(theme) + unifiedScrollBarQss(theme);
}

QString ApplyStyle::addWindowDialogStyle()
{
    return addWindowDialogStyle(MainWindowTheme::Default);
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
};

const AggregateChatTokens kAggDefault = {
    "#F4F4F5",
    "#18181B",
    "#FFFFFF",
    "#F4F4F5",
    "#E4E4E7",
    "#71717A",
    "#52525B",
    "#3F3F46",
    "#18181B",
    "rgba(24,24,27,0.06)",
    "#D4D4D8",
    "#FFFFFF",
    "#18181B",
    "#18181B",
    "rgba(24,24,27,0.04)",
    "#E4E4E7",
    "#F4F4F5",
    "#18181B",
    "#71717A",
    "#FFFFFF",
    "#18181B",
    "#E4E4E7",
    "#F4F4F5",
    "#E4E4E7",
    "#FFFFFF",
    "#D4D4D8",
    "#18181B",
    "#18181B",
    "#FFFFFF",
    "#E4E4E7",
    "#6B7280",
    "#18181B",
    "#FFFFFF",
    "#71717A",
    "#D4D4D8",
    "#E4E4E7",
    "#71717A",
    "#E4E4E7",
    "#F4F4F5",
    "#18181B",
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
};

QString buildAggregateChatFormQss(const AggregateChatTokens& t)
{
    return QStringLiteral(
        R"QSS(
        AggregateChatForm {
            background: %1;
        }
        QLabel {
            color: %2;
        }
        QLabel#aggregateModeLabel {
            color: %2;
            font-size: 12px;
            font-weight: 600;
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
        }
        AggregateChatForm QComboBox:focus {
            border-color: %14;
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
            background: qlineargradient(
                x1:0, y1:0, x2:0, y2:1,
                stop:0 %3,
                stop:1 %4
            );
            border-right: 1px solid %5;
        }
        QPushButton#simulateButton {
            background: %6;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 5px 12px;
            font-size: 12px;
        }
        QPushButton#simulateButton:hover {
            background: %7;
        }
        QPushButton#simulateButton:pressed {
            background: %8;
        }
        QPushButton#aggregateTabPending {
            background: transparent;
            color: %9;
            border: none;
            border-radius: 6px;
            padding: 6px 14px;
            font-size: 13px;
        }
        QPushButton#aggregateTabPending:checked {
            background: %6;
            color: white;
        }
        QPushButton#aggregateTabPending:!checked {
            background: transparent;
            color: %9;
        }
        QPushButton#aggregateTabPending:hover:!checked {
            background: %10;
        }
        QPushButton#aggregateTabAll {
            background: transparent;
            color: %9;
            border: none;
            border-radius: 6px;
            padding: 6px 14px;
            font-size: 13px;
        }
        QPushButton#aggregateTabAll:checked {
            background: %6;
            color: white;
        }
        QPushButton#aggregateTabAll:hover:!checked {
            background: %10;
        }
        QLineEdit#aggregateSearch {
            padding: 6px 8px;
            border: 1px solid %11;
            border-radius: 8px;
            font-size: 13px;
            background: %12;
            color: %13;
        }
        QLineEdit#aggregateSearch:focus {
            border-color: %14;
        }
        QWidget#aggregateListEmpty {
            background: transparent;
        }
        QLabel#aggregateListEmptyText {
            font-size: 12px;
            color: %2;
        }
        QListWidget#aggregateConversationList {
            background: transparent;
            border: none;
            outline: none;
        }
        QListWidget#aggregateConversationList::item {
            padding: 2px 4px;
            border-radius: 6px;
        }
        QListWidget#aggregateConversationList::item:hover {
            background: %15;
        }
        QListWidget#aggregateConversationList::item:selected {
            background: %16;
            border-radius: 8px;
        }

        QWidget#aggregateCenterPanel {
            background: %17;
        }
        QWidget#chatArea {
            background: %17;
        }
        QSplitter::handle {
            background: %17;
            border: none;
        }
        QFrame#aggregateEmptyIcon {
            background: %18;
            border-radius: 16px;
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

        QLabel#chatHeader {
            background: %21;
            font-size: 14px;
            font-weight: bold;
            color: %22;
            border-bottom: 1px solid %23;
        }
        QScrollArea#messageScroll {
            background: %17;
            border: none;
        }
        QScrollArea#messageScroll QWidget {
            background: transparent;
        }
        QFrame#inputDivider {
            color: %24;
        }
        QPlainTextEdit#messageInput {
            background: %25;
            border: 1px solid %26;
            border-radius: 8px;
            padding: 10px;
            font-size: 13px;
            color: %27;
        }
        QPlainTextEdit#messageInput:focus {
            border-color: %28;
        }
        QPushButton#sendButton {
            background: %6;
            color: white;
            border: none;
            border-radius: 8px;
            padding: 8px 18px;
            font-size: 13px;
            font-weight: bold;
        }
        QPushButton#sendButton:hover {
            background: %7;
        }
        QPushButton#sendButton:pressed {
            background: %8;
        }

        QWidget#chatArea QFrame#bubbleIn {
            background: %29;
            border: 1px solid %30;
            border-radius: 14px;
            border-top-left-radius: 4px;
        }
        QWidget#chatArea QFrame#bubbleOut {
            background: %31;
            border: none;
            border-radius: 14px;
            border-top-right-radius: 4px;
        }
        QLabel#bubbleTextIn {
            font-size: 13px;
            color: %32;
        }
        QLabel#bubbleTextOut {
            font-size: 13px;
            color: %33;
        }
        QLabel#bubbleMetaIn {
            font-size: 10px;
            color: %34;
        }
        QLabel#bubbleMetaOut {
            font-size: 10px;
            color: %35;
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
        QFrame#dateSeparatorLine {
            background: %36;
            border: none;
        }
        QLabel#dateSeparatorText {
            font-size: 11px;
            color: %37;
            background: transparent;
        }

        QWidget#aggregateRightPanel {
            background: qlineargradient(
                x1:0, y1:0, x2:0, y2:1,
                stop:0 %3,
                stop:1 %4
            );
        }
        QWidget#aggregateRightHeader {
            background: qlineargradient(
                x1:0, y1:0, x2:0, y2:1,
                stop:0 %3,
                stop:1 %4
            );
        }
        QLabel#aggregateRightHeaderTitle {
            font-size: 14px;
            font-weight: bold;
            color: %38;
        }
        QLabel#aggregateRightHeaderSub {
            font-size: 12px;
            color: %38;
        }
        QFrame#aggregateRightEmptyIcon {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %39, stop:1 %40);
            border-radius: 14px;
        }
        QLabel#aggregateRightEmptyMain {
            font-size: 12px;
            font-weight: bold;
            color: %38;
        }
        QLabel#aggregateRightEmptySub {
            font-size: 11px;
            color: %38;
        }
        QLabel#customerName {
            font-size: 14px;
            font-weight: bold;
            color: %38;
        }
        QLabel#customerPlatform {
            font-size: 12px;
            color: %38;
        }
        QLabel#customerStatus {
            font-size: 12px;
            color: %38;
        }
        QLabel#sendTimelineTitle {
            font-size: 12px;
            font-weight: 600;
            color: %38;
        }
        QPlainTextEdit#sendStatusTimeline {
            background: %25;
            border: 1px solid %26;
            border-radius: 8px;
            padding: 8px;
            font-size: 12px;
            color: %27;
        }
        QPlainTextEdit#sendStatusTimeline:focus {
            border-color: %28;
        }
        QPushButton#sendTimelineClearBtn {
            background: %6;
            color: #ffffff;
            border: 1px solid %6;
            border-radius: 6px;
            padding: 6px 14px;
            font-size: 12px;
            font-weight: 600;
            min-height: 28px;
        }
        QPushButton#sendTimelineClearBtn:hover {
            background: %7;
            border-color: %7;
            color: #ffffff;
        }
        QPushButton#sendTimelineClearBtn:pressed {
            background: %8;
            border-color: %8;
            color: #ffffff;
        }

    )QSS")
        .arg(QLatin1String(t.pageBg))
        .arg(QLatin1String(t.labelCombo))
        .arg(QLatin1String(t.panelGradTop))
        .arg(QLatin1String(t.panelGradBottom))
        .arg(QLatin1String(t.panelBorder))
        .arg(QLatin1String(t.accent))
        .arg(QLatin1String(t.accentHover))
        .arg(QLatin1String(t.accentPressed))
        .arg(QLatin1String(t.tabText))
        .arg(QLatin1String(t.tabHoverBg))
        .arg(QLatin1String(t.searchBorder))
        .arg(QLatin1String(t.searchBg))
        .arg(QLatin1String(t.searchText))
        .arg(QLatin1String(t.searchFocus))
        .arg(QLatin1String(t.listHover))
        .arg(QLatin1String(t.listSelected))
        .arg(QLatin1String(t.areaBg))
        .arg(QLatin1String(t.emptyIconBg))
        .arg(QLatin1String(t.emptyMain))
        .arg(QLatin1String(t.emptySub))
        .arg(QLatin1String(t.chatHeaderBg))
        .arg(QLatin1String(t.chatHeaderText))
        .arg(QLatin1String(t.chatHeaderBorder))
        .arg(QLatin1String(t.inputDivider))
        .arg(QLatin1String(t.inputBg))
        .arg(QLatin1String(t.inputBorder))
        .arg(QLatin1String(t.inputText))
        .arg(QLatin1String(t.inputFocus))
        .arg(QLatin1String(t.bubbleInBg))
        .arg(QLatin1String(t.bubbleInBorder))
        .arg(QLatin1String(t.bubbleOutBg))
        .arg(QLatin1String(t.bubbleTextIn))
        .arg(QLatin1String(t.bubbleTextOut))
        .arg(QLatin1String(t.bubbleMetaIn))
        .arg(QLatin1String(t.bubbleMetaOut))
        .arg(QLatin1String(t.dateLine))
        .arg(QLatin1String(t.dateText))
        .arg(QLatin1String(t.rightText))
        .arg(QLatin1String(t.rightEmptyG1))
        .arg(QLatin1String(t.rightEmptyG2))
        .arg(QLatin1String(t.bubbleMetaIn))
        .arg(QLatin1String(t.chatHeaderText));
}

} // namespace

QString ApplyStyle::aggregateChatFormStyle()
{
    return aggregateChatFormStyle(MainWindowTheme::Default);
}

QString ApplyStyle::aggregateChatFormStyle(MainWindowTheme theme)
{
    switch (theme) {
    case MainWindowTheme::Cool:
        return buildAggregateChatFormQss(kAggCool) + unifiedScrollBarQss(MainWindowTheme::Cool);
    case MainWindowTheme::Warm:
        return buildAggregateChatFormQss(kAggWarm) + unifiedScrollBarQss(MainWindowTheme::Warm);
    case MainWindowTheme::Default:
    default:
        return buildAggregateChatFormQss(kAggDefault) + unifiedScrollBarQss(MainWindowTheme::Default);
    }
}

QString ApplyStyle::helpDialogHtmlBodyTextColor(MainWindowTheme theme)
{
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

QString ApplyStyle::helpDialogStyle(MainWindowTheme theme)
{
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

            QTreeWidget#helpToc {
                background: #111827;
                border: 1px solid #334155;
                border-radius: 12px;
                outline: none;
                font-size: 13px;
                color: #e2e8f0;
                padding: 8px 4px;
                show-decoration-selected: 0;
            }
            QTreeWidget#helpToc:focus {
                outline: none;
            }
            QTreeWidget#helpToc::item {
                padding: 8px 12px;
                border-radius: 8px;
                min-height: 22px;
                outline: none;
            }
            QTreeWidget#helpToc::item:selected {
                background: #0284c7;
                color: #f8fafc;
                border: none;
            }
            QTreeWidget#helpToc::item:hover:!selected {
                background: #1e293b;
                color: #f1f5f9;
            }
            QTreeWidget#helpToc::item:focus {
                outline: none;
                border: none;
            }

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

            QTreeWidget#helpToc {
                background: #2a2624;
                border: 1px solid #5c4f42;
                border-radius: 12px;
                outline: none;
                font-size: 13px;
                color: #f5efe6;
                padding: 8px 4px;
                show-decoration-selected: 0;
            }
            QTreeWidget#helpToc:focus {
                outline: none;
            }
            QTreeWidget#helpToc::item {
                padding: 8px 12px;
                border-radius: 8px;
                min-height: 22px;
                outline: none;
            }
            QTreeWidget#helpToc::item:selected {
                background: #b45309;
                color: #fffbeb;
                border: none;
            }
            QTreeWidget#helpToc::item:hover:!selected {
                background: #3d3835;
                color: #fff7ed;
            }
            QTreeWidget#helpToc::item:focus {
                outline: none;
                border: none;
            }

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

            QTreeWidget#helpToc {
                background: #F4F4F5;
                border: 1px solid #D4D4D8;
                border-radius: 12px;
                outline: none;
                font-size: 13px;
                color: #27272A;
                padding: 8px 4px;
                show-decoration-selected: 0;
            }
            QTreeWidget#helpToc:focus {
                outline: none;
            }
            QTreeWidget#helpToc::item {
                padding: 8px 12px;
                border-radius: 8px;
                min-height: 22px;
                outline: none;
            }
            QTreeWidget#helpToc::item:selected {
                background: #3F3F46;
                color: #FAFAFA;
                border: none;
            }
            QTreeWidget#helpToc::item:hover:!selected {
                background: #E4E4E7;
                color: #18181B;
            }
            QTreeWidget#helpToc::item:focus {
                outline: none;
                border: none;
            }

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
            border: 2px solid #A1A1AA;
        }
        QTreeWidget#quickLaunchAppTree::indicator:unchecked:hover,
        QTreeWidget#quickLaunchRulesTree::indicator:unchecked:hover {
            border-color: #71717A;
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
    "#F4F4F5",
    "#18181B",
    "#FFFFFF",
    "#E4E4E7",
    "rgba(24,24,27,0.06)",
    "#D4D4D8",
    "#18181B",
    "#FFFFFF",
    "#D4D4D8",
    "#18181B",
    "#8B8B94",
    "#73737C",
    "#5C5C65",
    "#F4F4F5",
    "#FAFAFA",
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
    switch (theme) {
    case MainWindowTheme::Cool:
        return buildStatusMessageManagerQss(kStatusCool) + unifiedScrollBarQss(MainWindowTheme::Cool);
    case MainWindowTheme::Warm:
        return buildStatusMessageManagerQss(kStatusWarm) + unifiedScrollBarQss(MainWindowTheme::Warm);
    case MainWindowTheme::Default:
    default:
        return buildStatusMessageManagerQss(kStatusDefault) + unifiedScrollBarQss(MainWindowTheme::Default);
    }
}

QString ApplyStyle::quickLaunchManagerStyle(MainWindowTheme theme)
{
    QString base;
    switch (theme) {
    case MainWindowTheme::Cool:
        base = buildQuickLaunchManagerQss(kStatusCool);
        break;
    case MainWindowTheme::Warm:
        base = buildQuickLaunchManagerQss(kStatusWarm);
        break;
    case MainWindowTheme::Default:
    default:
        base = buildQuickLaunchManagerQss(kStatusDefault);
        break;
    }
    return base + quickLaunchManagerCheckboxQss(theme) + quickLaunchManagerTreeIndicatorAndScrollQss(theme)
        + unifiedScrollBarQss(theme);
}
