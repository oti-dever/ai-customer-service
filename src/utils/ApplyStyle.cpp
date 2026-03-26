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
};

const MainWindowStyleTokens kTokensDefault = {
    "#333333",
    "#333333",
    "#1f1f1f",
    "#ffffff",
    "#edf0f5",
    "#2a2a2a",
    "#ffffff",
    "#eef1f6",
    "#2a2a2a",
    "#f4f6f9",
    "#ffffff",
    "#eef1f6",
    "#eefaf5",
    "#2a2a2a",
    "#edf0f5",
    "#6b7280",
    "#ffffff",
    "#eef1f6",
    "#3b3b3b",
    "#fbfcfe",
    "#e6ebf5",
    "#f3f5fb",
    "#2b2f36",
    "#f5f7fa",
    "#3a404a",
    "#f5f7fa",
    "#f5f7fa",
    "#4a5568",
    "#3a404a",
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
};

QString buildMainWindowStyleQss(const MainWindowStyleTokens& t)
{
    return QStringLiteral(
        R"QSS(
        QWidget#root { background: %1; }

        QWidget#rightArea { background: %2; }
        QWidget#leftSidebar { background: %3; }
        QTreeView#platformList {
            background: transparent;
            border: none;
            outline: none;
        }
        QTreeView#platformList::item { background: transparent; }

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
        .arg(QLatin1String(t.themeButtonHoverBg));
}

} // namespace

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
        c.groupBgDefault = QColor(245, 245, 247);
        c.groupBgHover = QColor(238, 240, 245);
        c.groupBgSelected = QColor(232, 235, 245);
        c.groupTextColor = QColor(40, 40, 40);
        c.groupArrowColor = QColor(120, 120, 120);
        c.itemBgDefault = QColor(255, 255, 255);
        c.itemBgHover = QColor(248, 249, 252);
        c.itemBgSelected = QColor(235, 243, 255);
        c.itemTextColor = QColor(40, 40, 40);
        c.itemAccentBarColor = QColor(24, 144, 255);
        c.itemInactiveBgDefault = QColor(248, 248, 250);
        c.itemInactiveBgHover = QColor(244, 244, 248);
        c.itemInactiveBgSelected = QColor(240, 242, 250);
        c.itemInactiveTextColor = QColor(170, 170, 170);
        c.csDotActivated = QColor(82, 196, 26);
        c.csDotInactive = QColor(190, 190, 190);
        c.itemInactiveIconOpacity = 0.35;
        return c;
    }
    }
}

QString ApplyStyle::loginWindowStyle()
{
    return QStringLiteral(R"QSS(
        LoginWindow {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #fddebd, stop:1 #abd8df);
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
        return buildMainWindowStyleQss(kTokensCool);
    case MainWindowTheme::Warm:
        return buildMainWindowStyleQss(kTokensWarm);
    case MainWindowTheme::Default:
    default:
        return buildMainWindowStyleQss(kTokensDefault);
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

QString ApplyStyle::addWindowDialogStyle()
{
    /* 配色：#f7dec0 #c0d9f7 #deebfb #ffffff（已去掉 #f3cea2） */
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
        QDialog QPushButton:hover,
        QDialog QPushButton:default:hover {
            background-color: #a8c9ef;
            border-color: #8eb4e0;
        }
        QDialog QPushButton:pressed,
        QDialog QPushButton:default:pressed {
            background-color: #8eb4e0;
            border-color: #7aa8d4;
        }
        QDialog QPushButton:default {
            color: #1e293b;
            background-color: #c0d9f7;
            border-color: #a8c9ef;
            font-weight: 600;
        }
        QDialog QTableWidget {
            background-color: #ffffff;
            alternate-background-color: #eef6fc;
            color: #1e293b;
            gridline-color: #c0d9f7;
            border: 1px solid #c0d9f7;
            border-radius: 10px;
            font-size: 13px;
        }
        QDialog QTableWidget::item {
            color: #1e293b;
            padding: 4px;
        }
        QDialog QTableWidget::item:selected {
            background-color: #b8d4f5;
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
        }
    )QSS");
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
    "#E0F2FE",
    "#000000",
    "#BAE6FD",
    "#E0F2FE",
    "#BAE6FD",
    "#0EA5E9",
    "#38BDF8",
    "#0EA5E9",
    "#0f172a",
    "rgba(14,165,233,0.08)",
    "#BAE6FD",
    "rgba(255,255,255,0.85)",
    "#0f172a",
    "#0EA5E9",
    "rgba(148, 210, 255, 0.25)",
    "#7DD3FC",
    "#7DD3FC",
    "#333333",
    "#6b7280",
    "#E0F2FE",
    "#0f172a",
    "#BAE6FD",
    "#E0F2FE",
    "#e0e0e0",
    "#ffffff",
    "#BAE6FD",
    "#0f172a",
    "#0EA5E9",
    "#BAE6FD",
    "#7DD3FC",
    "#0EA5E9",
    "#0f172a",
    "#ffffff",
    "#64748b",
    "rgba(255, 255, 255, 0.75)",
    "#BAE6FD",
    "#6b7280",
    "#f0f3f6",
    "#e8ecf0",
    "#000000",
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
        QLabel, QComboBox {
            color: %2;
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
        QScrollArea#messageScroll QScrollBar:vertical {
            width: 0px;
            background: transparent;
        }
        QScrollArea#messageScroll QScrollBar::handle:vertical {
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
        .arg(QLatin1String(t.rightEmptyG2));
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
        return buildAggregateChatFormQss(kAggCool);
    case MainWindowTheme::Warm:
        return buildAggregateChatFormQss(kAggWarm);
    case MainWindowTheme::Default:
    default:
        return buildAggregateChatFormQss(kAggDefault);
    }
}

QString ApplyStyle::helpDialogHtmlBodyTextColor(MainWindowTheme theme)
{
    switch (theme) {
    case MainWindowTheme::Cool:
        return QStringLiteral("#e2e8f0");
    case MainWindowTheme::Warm:
        return QStringLiteral("#fff7ed");
    case MainWindowTheme::Default:
    default:
        return QStringLiteral("#e2e8f0");
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
        return QStringLiteral("#334155");
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
    switch (theme) {
    case MainWindowTheme::Cool:
        return QStringLiteral(R"QSS(
            HelpDialog {
                background: #0f172a;
            }
            QSplitter {
                background: #0f172a;
            }
            QSplitter::handle {
                background: #334155;
                width: 1px;
            }

            #helpToc {
                background: #111827;
                border: none;
                outline: none;
                font-size: 13px;
                color: #e2e8f0;
                padding: 6px 0;
            }
            #helpToc::item {
                padding: 7px 12px;
                border: none;
            }
            #helpToc::item:selected {
                background: #075985;
                color: #7dd3fc;
                border-left: 3px solid #7dd3fc;
            }
            #helpToc::item:hover:!selected {
                background: #0b1220;
            }
            #helpToc::item:disabled {
                color: #64748b;
                font-weight: bold;
                font-size: 12px;
                padding: 10px 8px 4px 8px;
                background: transparent;
            }

            #helpBrowser {
                background: #0f172a;
                border: none;
                padding: 8px 16px 8px 16px;
                font-size: 13px;
                color: #e2e8f0;
                selection-background-color: #38bdf8;
                selection-color: #0f172a;
            }
        )QSS");
    case MainWindowTheme::Warm:
        return QStringLiteral(R"QSS(
            HelpDialog {
                background: #3d3835;
            }
            QSplitter {
                background: #3d3835;
            }
            QSplitter::handle {
                background: #7a5b43;
                width: 1px;
            }

            #helpToc {
                background: #2a2624;
                border: none;
                outline: none;
                font-size: 13px;
                color: #f5efe6;
                padding: 6px 0;
            }
            #helpToc::item {
                padding: 7px 12px;
                border: none;
            }
            #helpToc::item:selected {
                background: #b45309;
                color: #fff7ed;
                border-left: 3px solid #f59e0b;
            }
            #helpToc::item:hover:!selected {
                background: #2a2624;
            }
            #helpToc::item:disabled {
                color: #9a3412;
                font-weight: bold;
                font-size: 12px;
                padding: 10px 8px 4px 8px;
                background: transparent;
            }

            #helpBrowser {
                background: #3d3835;
                border: none;
                padding: 8px 16px 8px 16px;
                font-size: 13px;
                color: #fff7ed;
                selection-background-color: #f59e0b;
                selection-color: #2a2624;
            }
        )QSS");
    case MainWindowTheme::Default:
    default:
        return QStringLiteral(R"QSS(
            HelpDialog {
                background: #1e293b;
            }
            QSplitter {
                background: #1e293b;
            }
            QSplitter::handle {
                background: #334155;
                width: 1px;
            }

            #helpToc {
                background: #0f172a;
                border: none;
                outline: none;
                font-size: 13px;
                color: #cbd5e1;
                padding: 6px 0;
            }
            #helpToc::item {
                padding: 7px 12px;
                border: none;
            }
            #helpToc::item:selected {
                background: #1e3a5f;
                color: #38bdf8;
                border-left: 3px solid #38bdf8;
            }
            #helpToc::item:hover:!selected {
                background: #1e293b;
            }
            #helpToc::item:disabled {
                color: #64748b;
                font-weight: bold;
                font-size: 12px;
                padding: 10px 8px 4px 8px;
                background: transparent;
            }

            #helpBrowser {
                background: #1e293b;
                border: none;
                padding: 8px 16px 8px 16px;
                font-size: 13px;
                color: #e2e8f0;
                selection-background-color: #38bdf8;
                selection-color: #0f172a;
            }
        )QSS");
    }
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
            border: 1px solid %4;
            border-radius: 10px;
            color: %2;
        }
        QTreeWidget#quickLaunchAppTree::item,
        QTreeWidget#quickLaunchRulesTree::item {
            padding: 4px 8px;
            color: %2;
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
        QTreeWidget#quickLaunchAppTree QHeaderView::section,
        QTreeWidget#quickLaunchRulesTree QHeaderView::section {
            background: %3;
            color: %2;
            border: none;
            border-bottom: 1px solid %4;
            padding: 6px 8px;
            font-weight: 600;
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
        .arg(QLatin1String(t.accentPressed));
}

const SimpleManagerDialogTokens kStatusDefault = {
    "#ffffff",
    "#111827",
    "#ffffff",
    "#e5e7eb",
    "rgba(14,165,233,0.10)",
    "#0EA5E9",
    "#ffffff",
    "#ffffff",
    "#e5e7eb",
    "#111827",
    "#0EA5E9",
    "#38BDF8",
    "#0284c7",
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
};

} // namespace

QString ApplyStyle::statusMessageManagerStyle(MainWindowTheme theme)
{
    switch (theme) {
    case MainWindowTheme::Cool:
        return buildStatusMessageManagerQss(kStatusCool);
    case MainWindowTheme::Warm:
        return buildStatusMessageManagerQss(kStatusWarm);
    case MainWindowTheme::Default:
    default:
        return buildStatusMessageManagerQss(kStatusDefault);
    }
}

QString ApplyStyle::quickLaunchManagerStyle(MainWindowTheme theme)
{
    switch (theme) {
    case MainWindowTheme::Cool:
        return buildQuickLaunchManagerQss(kStatusCool);
    case MainWindowTheme::Warm:
        return buildQuickLaunchManagerQss(kStatusWarm);
    case MainWindowTheme::Default:
    default:
        return buildQuickLaunchManagerQss(kStatusDefault);
    }
}

QString ApplyStyle::quickLaunchHelpMessageBoxStyle(MainWindowTheme theme)
{
    switch (theme) {
    case MainWindowTheme::Cool:
        return QStringLiteral(R"QSS(
            QMessageBox {
                background: #0f172a;
            }
            QMessageBox QLabel {
                color: #e2e8f0;
                font-size: 13px;
            }
            QMessageBox QPushButton {
                background: #0284c7;
                color: #ffffff;
                border: none;
                border-radius: 8px;
                padding: 6px 16px;
                font-size: 13px;
            }
            QMessageBox QPushButton:hover {
                background: #0ea5e9;
            }
        )QSS");
    case MainWindowTheme::Warm:
        return QStringLiteral(R"QSS(
            QMessageBox {
                background: #f5efe6;
            }
            QMessageBox QLabel {
                color: #3d3429;
                font-size: 13px;
            }
            QMessageBox QPushButton {
                background: #b45309;
                color: #ffffff;
                border: none;
                border-radius: 8px;
                padding: 6px 16px;
                font-size: 13px;
            }
            QMessageBox QPushButton:hover {
                background: #d97706;
            }
        )QSS");
    case MainWindowTheme::Default:
    default:
        return QStringLiteral(R"QSS(
            QMessageBox {
                background: #ffffff;
            }
            QMessageBox QLabel {
                color: #111827;
                font-size: 13px;
            }
            QMessageBox QPushButton {
                background: #0EA5E9;
                color: #ffffff;
                border: none;
                border-radius: 8px;
                padding: 6px 16px;
                font-size: 13px;
            }
            QMessageBox QPushButton:hover {
                background: #38BDF8;
            }
        )QSS");
    }
}
