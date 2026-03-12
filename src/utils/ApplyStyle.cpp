#include "applystyle.h"

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

QString ApplyStyle::mainWindowStyle()
{
    return QStringLiteral(R"QSS(
        /* ----- 根区域 ----- */
        QWidget#root { background: #333333; }

        QWidget#rightArea {background: #333333; }
        /* ----- 左侧边栏 ----- */
        QWidget#leftSidebar { background: #1f1f1f; }
        QTreeView#platformList {
            background: transparent;
            border: none;
            outline: none;
        }
        QTreeView#platformList::item { background: transparent; }

        // QToolButton#settingsButton {
        //     background: #F5F5F7;
        //     border: none;
        //     border-radius: 8px;
        //     color: #333333;
        //     font-size: 14px;
        //     font-weight: bold;
        //     text-align: left;
        //     padding-left: 12px;
        // }
        // QToolButton#settingsButton:hover {
        //     background: #E8E8EB;
        // }
        // QToolButton#settingsButton:pressed {
        //     background: #DFDFE2;
        // }

        /* ----- 顶部栏 ----- */
        QWidget#topBar {
            background: #ffffff;
            border-bottom: 1px solid #edf0f5;
        }
        QLabel#topTitle {
            color: #2a2a2a;
            font-size: 14px;
            font-weight: 600;
        }
        QWidget#readyWrap {
            background: #ffffff;
            border: 1px solid #eef1f6;
            border-radius: 12px;
        }
        QLabel#readyText {
            color: #2a2a2a;
            font-size: 16px;
            font-weight: 600;
        }

        /* ----- 中心区域 ----- */
        QWidget#centerArea, QWidget#centerStack, QWidget#placeholderPage {
            background: #f4f6f9;
        }

        /* ----- 卡片 ----- */
        QFrame#readyCard, QFrame#platformCard {
            background: #ffffff;
            border-radius: 14px;
            border: 1px solid #eef1f6;
        }
        QFrame#rocketWrap {
            background: #eefaf5;
            border-radius: 20px;
        }

        /* ----- 就绪页 ----- */
        QLabel#readyTitle {
            color: #2a2a2a;
            font-size: 22px;
            font-weight: 700;
        }
        QFrame#divider {
            background: #edf0f5;
        }
        QLabel#readySubtitle {
            color: #6b7280;
            font-size: 13px;
        }
        QLabel#placeholderText {
            color: #6b7280;
            font-size: 14px;
        }

        /* ----- 快捷卡片 ----- */
        QToolButton#quickCard {
            background: #ffffff;
            border: 1px solid #eef1f6;
            border-radius: 12px;
            padding: 10px;
            color: #3b3b3b;
            font-size: 13px;
        }
        QToolButton#quickCard:hover {
            background: #fbfcfe;
            border-color: #e6ebf5;
        }
        QToolButton#quickCard:pressed {
            background: #f3f5fb;
        }

        /* ----- 状态栏 ----- */
        QStatusBar {
            background: #2b2f36;
            color: #f5f7fa;
            font-size: 12px;
            border-top: 1px solid #3a404a;
        }
        QStatusBar::item { border: none; }
        QWidget#statusBarWrap {
            background: transparent;
        }
        QLabel#statusMessage,
        QLabel#statusSeparator,
        QLabel#statusTime {
            color: #f5f7fa;
            background: transparent;
        }
    )QSS");
}

QString ApplyStyle::aggregateChatFormStyle()
{
    return QStringLiteral(R"QSS(
        /* ----- 聚合对话接待窗口（管理后台） ----- */
        QMainWindow {
            background: #f9fafb;
        }
        QWidget#aggregateLeftPanel {
            background: #2d2d30;
            border-right: 1px solid #3e3e42;
        }
        QLabel#aggregateLeftTitle {
            font-size: 14px;
            font-weight: bold;
            color: #e8e8e8;
        }
        QPushButton#simulateButton {
            background: #52c41a;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 4px 10px;
            font-size: 12px;
        }
        QPushButton#simulateButton:hover {
            background: #73d13d;
        }
        QPushButton#simulateButton:pressed {
            background: #389e0d;
        }
        QPushButton#aggregateTabPending {
            background: #ff7d00;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 6px 12px;
            font-size: 13px;
        }
        QPushButton#aggregateTabPending:checked {
            background: #ff7d00;
            color: white;
        }
        QPushButton#aggregateTabPending:!checked {
            background: #444;
            color: #ccc;
        }
        QPushButton#aggregateTabPending:hover:!checked {
            background: #555;
        }
        QPushButton#aggregateTabAll {
            background: #444;
            color: #ccc;
            border: none;
            border-radius: 4px;
            padding: 6px 12px;
            font-size: 13px;
        }
        QPushButton#aggregateTabAll:checked {
            background: #ff7d00;
            color: white;
        }
        QPushButton#aggregateTabAll:hover:!checked {
            background: #555;
        }
        QLineEdit#aggregateSearch {
            padding: 8px 12px 8px 36px;
            border: 1px solid #3e3e42;
            border-radius: 6px;
            font-size: 13px;
            background: #3c3c3f;
            color: #e8e8e8;
        }
        QLineEdit#aggregateSearch:focus {
            border-color: #1890FF;
        }
        QWidget#aggregateListEmpty {
            background: transparent;
        }
        QLabel#aggregateListEmptyText {
            font-size: 12px;
            color: #888;
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
            background: rgba(255,255,255,0.06);
        }
        QListWidget#aggregateConversationList::item:selected {
            background: rgba(24,144,255,0.2);
        }

        QWidget#aggregateCenterPanel {
            background: #f5f5f5;
        }
        QFrame#aggregateEmptyIcon {
            background: #ff7d00;
            border-radius: 12px;
        }
        QLabel#aggregateEmptyMain {
            font-size: 16px;
            font-weight: bold;
            color: #333;
        }
        QLabel#aggregateEmptySub {
            font-size: 12px;
            color: #6b7280;
        }

        /* ----- Chat area ----- */
        QLabel#chatHeader {
            background: #ffffff;
            font-size: 14px;
            font-weight: bold;
            color: #333;
            border-bottom: 1px solid #e8e8e8;
        }
        QScrollArea#messageScroll {
            background: #f5f5f5;
            border: none;
        }
        QFrame#inputDivider {
            color: #e0e0e0;
        }
        QPlainTextEdit#messageInput {
            background: #ffffff;
            border: 1px solid #d9d9d9;
            border-radius: 6px;
            padding: 8px;
            font-size: 13px;
            color: #333;
        }
        QPlainTextEdit#messageInput:focus {
            border-color: #1890FF;
        }
        QPushButton#sendButton {
            background: #1890FF;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 8px 16px;
            font-size: 13px;
            font-weight: bold;
        }
        QPushButton#sendButton:hover {
            background: #40a9ff;
        }
        QPushButton#sendButton:pressed {
            background: #096dd9;
        }

        /* ----- Message bubbles ----- */
        QFrame#bubbleIn {
            background: #ffffff;
            border: 1px solid #e8e8e8;
            border-radius: 12px;
            border-top-left-radius: 2px;
        }
        QFrame#bubbleOut {
            background: #1890FF;
            border: none;
            border-radius: 12px;
            border-top-right-radius: 2px;
        }
        QLabel#bubbleTextIn {
            font-size: 13px;
            color: #333;
        }
        QLabel#bubbleTextOut {
            font-size: 13px;
            color: #ffffff;
        }
        QLabel#bubbleMeta {
            font-size: 10px;
            color: #999;
        }

        /* ----- Right panel ----- */
        QWidget#aggregateRightPanel {
            background: #FFFFFF;
        }
        QWidget#aggregateRightHeader {
            background: #1f2937;
        }
        QLabel#aggregateRightHeaderTitle {
            font-size: 14px;
            font-weight: bold;
            color: white;
        }
        QLabel#aggregateRightHeaderSub {
            font-size: 12px;
            color: rgba(255,255,255,0.8);
        }
        QPushButton#aggregateRightHeaderClose {
            background: transparent;
            color: white;
            border: none;
            font-size: 18px;
        }
        QPushButton#aggregateRightHeaderClose:hover {
            background: rgba(255,255,255,0.15);
            border-radius: 4px;
        }
        QFrame#aggregateRightEmptyIcon {
            background: #f3f4f6;
            border-radius: 12px;
        }
        QLabel#aggregateRightEmptyMain {
            font-size: 12px;
            font-weight: bold;
            color: #333;
        }
        QLabel#aggregateRightEmptySub {
            font-size: 11px;
            color: #6b7280;
        }
        QLabel#customerName {
            font-size: 14px;
            font-weight: bold;
            color: #333;
        }
        QLabel#customerPlatform {
            font-size: 12px;
            color: #666;
        }
        QLabel#customerStatus {
            font-size: 12px;
            color: #52c41a;
        }
    )QSS");
}
