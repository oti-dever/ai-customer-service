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
            background: #E0F2FE;
        }
        QLabel, QComboBox {
            color: #000000;
        }
        QWidget#aggregateLeftPanel {
            background: qlineargradient(
                x1:0, y1:0, x2:0, y2:1,
                stop:0 #BAE6FD,
                stop:1 #E0F2FE
            );
            border-right: 1px solid #BAE6FD;
        }
        QPushButton#simulateButton {
            background: #0EA5E9;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 5px 12px;
            font-size: 12px;
        }
        QPushButton#simulateButton:hover {
            background: #38BDF8;
        }
        QPushButton#simulateButton:pressed {
            background: #0EA5E9;
        }
        QPushButton#aggregateTabPending {
            background: transparent;
            color: #0f172a;
            border: none;
            border-radius: 6px;
            padding: 6px 14px;
            font-size: 13px;
        }
        QPushButton#aggregateTabPending:checked {
            background: #0EA5E9;
            color: white;
        }
        QPushButton#aggregateTabPending:!checked {
            background: transparent;
            color: #0f172a;
        }
        QPushButton#aggregateTabPending:hover:!checked {
            background: rgba(14,165,233,0.08);
        }
        QPushButton#aggregateTabAll {
            background: transparent;
            color: #0f172a;
            border: none;
            border-radius: 6px;
            padding: 6px 14px;
            font-size: 13px;
        }
        QPushButton#aggregateTabAll:checked {
            background: #0EA5E9;
            color: white;
        }
        QPushButton#aggregateTabAll:hover:!checked {
            background: rgba(14,165,233,0.08);
        }
        QLineEdit#aggregateSearch {
            padding: 6px 8px;
            border: 1px solid #BAE6FD;
            border-radius: 8px;
            font-size: 13px;
            background: rgba(255,255,255,0.85);
            color: #0f172a;
        }
        QLineEdit#aggregateSearch:focus {
            border-color: #0EA5E9;
        }
        QWidget#aggregateListEmpty {
            background: transparent;
        }
        QLabel#aggregateListEmptyText {
            font-size: 12px;
            color: #000000;
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
            background: rgba(148, 210, 255, 0.25);
        }
        QListWidget#aggregateConversationList::item:selected {
            background: #7DD3FC;
            border-radius: 8px;
        }

        QWidget#aggregateCenterPanel {
            background: #E0F2FE;
        }
        QWidget#chatArea {
            background: #E0F2FE;
        }
        QSplitter::handle {
            background: #E0F2FE;
            border: none;
        }
        QFrame#aggregateEmptyIcon {
            background: #7DD3FC;
            border-radius: 16px;
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
            background: #E0F2FE;
            font-size: 14px;
            font-weight: bold;
            color: #0f172a;
            border-bottom: 1px solid #BAE6FD;
        }
        QScrollArea#messageScroll {
            background: #E0F2FE;
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
            color: #e0e0e0;
        }
        QPlainTextEdit#messageInput {
            background: #ffffff;
            border: 1px solid #BAE6FD;
            border-radius: 8px;
            padding: 10px;
            font-size: 13px;
            color: #0f172a;
        }
        QPlainTextEdit#messageInput:focus {
            border-color: #0EA5E9;
        }
        QPushButton#sendButton {
            background: #0EA5E9;
            color: white;
            border: none;
            border-radius: 8px;
            padding: 8px 18px;
            font-size: 13px;
            font-weight: bold;
        }
        QPushButton#sendButton:hover {
            background: #38BDF8;
        }
        QPushButton#sendButton:pressed {
            background: #0EA5E9;
        }

        /* ----- Message bubbles ----- */
        QWidget#chatArea QFrame#bubbleIn {
            background: #BAE6FD;
            border: 1px solid #7DD3FC;
            border-radius: 14px;
            border-top-left-radius: 4px;
        }
        QWidget#chatArea QFrame#bubbleOut {
            background: #0EA5E9;
            border: none;
            border-radius: 14px;
            border-top-right-radius: 4px;
        }
        QLabel#bubbleTextIn {
            font-size: 13px;
            color: #0f172a;
        }
        QLabel#bubbleTextOut {
            font-size: 13px;
            color: #ffffff;
        }
        QLabel#bubbleMetaIn {
            font-size: 10px;
            color: #64748b;
        }
        QLabel#bubbleMetaOut {
            font-size: 10px;
            color: rgba(255, 255, 255, 0.75);
        }
        QFrame#dateSeparatorLine {
            background: #BAE6FD;
            border: none;
        }
        QLabel#dateSeparatorText {
            font-size: 11px;
            color: #6b7280;
            background: transparent;
        }

        /* ----- Right panel ----- */
        QWidget#aggregateRightPanel {
            background: qlineargradient(
                x1:0, y1:0, x2:0, y2:1,
                stop:0 #BAE6FD,
                stop:1 #E0F2FE
            );
        }
        QWidget#aggregateRightHeader {
            background: qlineargradient(
                x1:0, y1:0, x2:0, y2:1,
                stop:0 #BAE6FD,
                stop:1 #E0F2FE
            );
        }
        QLabel#aggregateRightHeaderTitle {
            font-size: 14px;
            font-weight: bold;
            color: #000000;
        }
        QLabel#aggregateRightHeaderSub {
            font-size: 12px;
            color: #000000;
        }
        QFrame#aggregateRightEmptyIcon {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f0f3f6, stop:1 #e8ecf0);
            border-radius: 14px;
        }
        QLabel#aggregateRightEmptyMain {
            font-size: 12px;
            font-weight: bold;
            color: #000000;
        }
        QLabel#aggregateRightEmptySub {
            font-size: 11px;
            color: #000000;
        }
        QLabel#customerName {
            font-size: 14px;
            font-weight: bold;
            color: #000000;
        }
        QLabel#customerPlatform {
            font-size: 12px;
            color: #000000;
        }
        QLabel#customerStatus {
            font-size: 12px;
            color: #000000;
        }

    )QSS");
}

QString ApplyStyle::helpDialogStyle()
{
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
            padding: 8px 16px;
            font-size: 13px;
            color: #e2e8f0;
            selection-background-color: #38bdf8;
            selection-color: #0f172a;
        }
    )QSS");
}
