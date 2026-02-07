#include "ApplyStyle.h"

QString ApplyStyle::robotManageFullStyle()
{
    // 组合所有样式，按层叠顺序
    return globalStyle() +
           leftNavStyle() +
           contentAreaStyle() +
           overviewPageStyle() +
           knowledgePageStyle() +
           messagePageStyle() +
           jargonPageStyle() +
           forbiddenPageStyle() +
           historyPageStyle() +
           backupPageStyle() +
           logPageStyle();
}

QString ApplyStyle::globalStyle()
{
    return QStringLiteral(R"QSS(
        QDialog { background: #ffffff; }
    )QSS");
}

QString ApplyStyle::leftNavStyle()
{
    return QStringLiteral(R"QSS(
        /* 左侧导航栏 #25262b */
        QWidget#robotNavSidebar { background: #25262b; }
        QWidget#navBrand { background: #25262b; }
        QLabel#navBrandTitle { color: #ffffff; font-size: 16px; font-weight: bold; }
        QLabel#navBrandSub { color: #ffffff; font-size: 12px; }
        QFrame#navDivider { background: #3d3e44; max-height: 1px; }
        QFrame#navStatCard { background: #3a3b40; border-radius: 4px; }
        QPushButton#navRefreshBtn, QToolButton#navRefreshBtn { color: #4080ff; border: none; background: transparent; }
        QLabel#navStatTitle { color: #c9cacd; font-size: 11px; }
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
    )QSS");
}

QString ApplyStyle::contentAreaStyle()
{
    return QStringLiteral(R"QSS(
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
    )QSS");
}

QString ApplyStyle::overviewPageStyle()
{
    return QStringLiteral(R"QSS(
        /* 系统概览页 */
        QFrame#overviewWelcomeBanner {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #1a1d29, stop:1 #2d3748);
            border: 1px solid #4a5568; border-radius: 8px;
        }
        QLabel#overviewWelcomeTitle { color: #ffffff; font-size: 20px; font-weight: bold; }
        QLabel#overviewWelcomeSub { color: #e2e8f0; font-size: 12px; }
        QLabel#overviewSectionTitle { color: #1d1d1f; font-size: 14px; font-weight: bold; }
        QLabel#overviewCardTitle { color: #8a8b90; font-size: 12px; }
        QLabel#overviewCardValue { color: #1d1d1f; font-size: 16px; font-weight: bold; }
        QFrame#overviewCardOrange, QFrame#overviewCardGreen, QFrame#overviewCardPurple,
        QFrame#overviewCardPink, QFrame#overviewCardBlue, QFrame#resCardPurple,
        QFrame#resCardBlue, QFrame#resCardGreen, QFrame#resCardPink, QFrame#resCardYellow {
            background: #ffffff; border: 1px solid #e5e6eb; border-radius: 8px;
        }
        QProgressBar#overviewRingGreen {
            border: 4px solid #e5e6eb; border-radius: 40px; background: #ffffff;
            text-align: center;
        }
        QProgressBar#overviewRingGreen::chunk { background: #00b42a; border-radius: 36px; }
        QProgressBar#overviewRingGray {
            border: 4px solid #e5e6eb; border-radius: 40px; background: #ffffff;
        }
        QProgressBar#overviewRingGray::chunk { background: #8a8b90; border-radius: 36px; }
        QProgressBar#overviewRingYellow {
            border: 4px solid #e5e6eb; border-radius: 40px; background: #ffffff;
        }
        QProgressBar#overviewRingYellow::chunk { background: #faad14; border-radius: 36px; }
        QLabel#overviewRingLabelGreen { color: #00b42a; font-size: 12px; font-weight: bold; }
        QLabel#overviewRingLabelGray { color: #8a8b90; font-size: 12px; }
        QLabel#overviewRingLabelYellow { color: #faad14; font-size: 12px; }
    )QSS");
}

QString ApplyStyle::knowledgePageStyle()
{
    return QStringLiteral(R"QSS(
        /* 知识库管理页 */
        QWidget#kbLeftPanel { background: #ffffff; border: 1px solid #e5e6eb; border-radius: 4px; padding: 12px; }
        QLineEdit#kbTreeSearch {
            background: #ffffff; border: 1px solid #e5e6eb; border-radius: 4px;
            padding: 8px 12px; color: #1d1d1f;
        }
        QPushButton#kbTreeSearchBtn { background: #9254de; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QPushButton#kbTreeAddBtn { background: #9254de; color: #ffffff; border: none; border-radius: 4px; font-size: 16px; font-weight: bold; }
        QTreeWidget#kbTree {
            background: #ffffff; border: none; outline: none;
            color: #1d1d1f; font-size: 13px;
        }
        QTreeWidget#kbTree::item {
            padding: 6px 4px; color: #1d1d1f;
        }
        QTreeWidget#kbTree::item:selected {
            color: #1d1d1f; background: #e6f4ff;
        }
        QTreeWidget#kbTree::item:hover {
            background: #f5f7fa;
        }
        QWidget#kbRightPanel {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #f9f5fc, stop:1 #f5eef8);
            border: 1px solid #e5e6eb; border-radius: 4px;
        }
        QLabel#kbRightTitle { color: #1d1d1f; font-size: 16px; font-weight: bold; }
        QLabel#kbRightSub { color: #8a8b90; font-size: 12px; }
        QFrame#kbEmptyPanel {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #f9f5fc, stop:1 #f5eef8);
            border: 1px solid #e5e6eb; border-radius: 8px;
        }
        QFrame#kbEmptyIconWrap { background: #e9e5ff; border-radius: 32px; }
        QLabel#kbEmptyTitle { color: #1d1d1f; font-size: 14px; font-weight: bold; }
        QLabel#kbEmptySub { color: #8a8b90; font-size: 12px; }
    )QSS");
}

QString ApplyStyle::messagePageStyle()
{
    return QStringLiteral(R"QSS(
        /* 消息处理页 */
        QLabel#msgSectionTitle { color: #1d1d1f; font-size: 14px; font-weight: bold; }
        QLabel#msgSectionHint { color: #8a8b90; font-size: 12px; }
        QFrame#msgCardRecv { background: #e6f4ff; border-radius: 8px; border: 1px solid #bae0ff; }
        QFrame#msgCardSend { background: #f0fdf4; border-radius: 8px; border: 1px solid #bbf7d0; }
        QFrame#msgCardStat { background: #faf5ff; border-radius: 8px; border: 1px solid #e9d5ff; }
        QFrame#msgCardQuick { background: #fff7ed; border-radius: 8px; border: 1px solid #fed7aa; }
        QFrame#msgCardRecv QLabel#msgCardTitle, QFrame#msgCardSend QLabel#msgCardTitle,
        QFrame#msgCardStat QLabel#msgCardTitle, QFrame#msgCardQuick QLabel#msgCardTitle {
            color: #1d1d1f; font-size: 13px; font-weight: bold;
        }
        QLabel#msgCardFooter, QLabel#msgStatRow { color: #8a8b90; font-size: 12px; }
        QLabel#msgQuickHint { color: #8a8b90; font-size: 11px; }
        QCheckBox#msgToggle { color: #1d1d1f; font-size: 12px; spacing: 8px; }
        QCheckBox#msgToggle::indicator { width: 40px; height: 20px; border-radius: 10px; background: #c0c0c0; }
        QCheckBox#msgToggle::indicator:checked { background: #3b82f6; }
        QPushButton#msgBtnGreen { background: #22c55e; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QPushButton#msgBtnGray { background: #ffffff; color: #4b5563; border: 1px solid #d1d5db; border-radius: 4px; padding: 0 16px; }
        QPushButton#msgBtnBlue { background: #3b82f6; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QFrame#msgFlowStep { background: #ffffff; border: 1px solid #e5e6eb; border-radius: 8px; }
        QFrame#msgFlowStepHighlight { background: #ffffff; border: 2px solid #00b42a; border-radius: 8px; }
        QLabel#msgFlowStepTitle { color: #1d1d1f; font-size: 12px; font-weight: bold; }
        QLabel#msgFlowStepSub { color: #8a8b90; font-size: 11px; }
        QLabel#msgSubSectionTitle { color: #1d1d1f; font-size: 13px; font-weight: bold; }
        QTextEdit#msgTextEdit {
            background: #ffffff; border: 1px solid #e5e6eb; border-radius: 4px;
            padding: 8px; color: #1d1d1f; font-size: 13px;
        }
        QLabel#msgCharCount { color: #8a8b90; font-size: 12px; }
    )QSS");
}

QString ApplyStyle::jargonPageStyle()
{
    return QStringLiteral(R"QSS(
        /* 行话转换页 */
        QLabel#jargonPlatformLabel { color: #1d1d1f; font-size: 13px; }
        QComboBox#jargonPlatformCombo {
            background: #f5f7fa; border: 1px solid #e5e6eb; border-radius: 4px;
            padding: 6px 12px; min-height: 20px; color: #1d1d1f;
        }
        QPushButton#jargonBtnTest {
            background: #f5f7fa; color: #1d1d1f; border: 1px solid #e5e6eb;
            border-radius: 4px; padding: 0 16px;
        }
        QPushButton#jargonBtnAdd { background: #8b5cf6; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QFrame#jargonCardTotal { background: #e6f4ff; border-radius: 8px; border: 1px solid #bae0ff; }
        QFrame#jargonCardEnabled { background: #f0fdf4; border-radius: 8px; border: 1px solid #bbf7d0; }
        QFrame#jargonCardReplace { background: #fff7ed; border-radius: 8px; border: 1px solid #fed7aa; }
        QFrame#jargonCardDelete { background: #fef2f2; border-radius: 8px; border: 1px solid #fecaca; }
        QLabel#jargonCardLabel { color: #8a8b90; font-size: 12px; }
        QLabel#jargonCardValue { color: #1d1d1f; font-size: 18px; font-weight: bold; }
        QLabel#jargonSectionTitle { color: #1d1d1f; font-size: 14px; font-weight: bold; }
        QLineEdit#jargonRulesSearch {
            background: #ffffff; border: 1px solid #e5e6eb; border-radius: 4px;
            padding: 8px 12px; color: #1d1d1f;
        }
        QTableWidget#jargonTable {
            background: #ffffff; border: 1px solid #e5e6eb; border-radius: 4px;
            gridline-color: #e5e6eb;
        }
        QTableWidget#jargonTable::item { color: #1d1d1f; padding: 8px; }
        QHeaderView::section {
            background: #f5f7fa; color: #8a8b90; font-size: 12px; padding: 10px 8px;
            border: none; border-bottom: 1px solid #e5e6eb;
        }
    )QSS");
}

QString ApplyStyle::forbiddenPageStyle()
{
    return QStringLiteral(R"QSS(
        /* 违禁词管理页 */
        QPushButton#forbiddenBtnTest {
            background: #f5f7fa; color: #1d1d1f; border: 1px solid #e5e6eb;
            border-radius: 4px; padding: 0 16px;
        }
        QPushButton#forbiddenBtnExport { background: #3b82f6; color: #ffffff; border: none; border-radius: 4px; padding: 0 12px; }
        QPushButton#forbiddenBtnImport { background: #22c55e; color: #ffffff; border: none; border-radius: 4px; padding: 0 12px; }
        QPushButton#forbiddenBtnAi { background: #8b5cf6; color: #ffffff; border: none; border-radius: 4px; padding: 0 12px; }
        QPushButton#forbiddenBtnBatchDir { background: #22c55e; color: #ffffff; border: none; border-radius: 4px; padding: 0 12px; }
        QPushButton#forbiddenBtnBatchEdit { background: #3b82f6; color: #ffffff; border: none; border-radius: 4px; padding: 0 12px; }
        QPushButton#forbiddenBtnBatchDel { background: #ef4444; color: #ffffff; border: none; border-radius: 4px; padding: 0 12px; }
        QPushButton#forbiddenBtnAdd { background: #8b5cf6; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QLabel#forbiddenPlatformLabel { color: #1d1d1f; font-size: 13px; }
        QComboBox#forbiddenPlatformCombo {
            background: #f5f7fa; border: 1px solid #e5e6eb; border-radius: 4px;
            padding: 6px 12px; min-height: 20px; color: #1d1d1f;
        }
        QLineEdit#forbiddenRuleSearch {
            background: #ffffff; border: 1px solid #e5e6eb; border-radius: 4px;
            padding: 8px 12px; color: #1d1d1f;
        }
        QFrame#forbiddenCardTotal { background: #e6f4ff; border-radius: 8px; border: 1px solid #bae0ff; }
        QFrame#forbiddenCardEnabled { background: #f0fdf4; border-radius: 8px; border: 1px solid #bbf7d0; }
        QFrame#forbiddenCardDisabled { background: #f3f4f6; border-radius: 8px; border: 1px solid #e5e7eb; }
        QFrame#forbiddenCardReplace { background: #faf5ff; border-radius: 8px; border: 1px solid #e9d5ff; }
        QLabel#forbiddenCardLabel { color: #8a8b90; font-size: 12px; }
        QLabel#forbiddenCardValue { color: #1d1d1f; font-size: 18px; font-weight: bold; }
        QTableWidget#forbiddenTable {
            background: #ffffff; border: 1px solid #e5e6eb; border-radius: 8px;
            gridline-color: #e5e6eb;
        }
        QTableWidget#forbiddenTable::item { color: #1d1d1f; padding: 8px; }
    )QSS");
}

QString ApplyStyle::historyPageStyle()
{
    return QStringLiteral(R"QSS(
        /* 对话历史页 */
        QLabel#historyMgmtTitle { color: #1d1d1f; font-size: 14px; font-weight: bold; }
        QLabel#historyMgmtSub { color: #8a8b90; font-size: 12px; }
        QLabel#historyTagGreen { color: #22c55e; font-size: 12px; background: #f3f4f6; border-radius: 4px; padding: 4px 10px; }
        QLabel#historyTagBlue { color: #3b82f6; font-size: 12px; background: #f3f4f6; border-radius: 4px; padding: 4px 10px; }
        QLabel#historyTagPurple { color: #8b5cf6; font-size: 12px; background: #f3f4f6; border-radius: 4px; padding: 4px 10px; }
        QPushButton#historyBtnRefresh {
            background: #f5f7fa; color: #1d1d1f; border: 1px solid #e5e6eb;
            border-radius: 4px; padding: 0 16px;
        }
        QPushButton#historyBtnExport { background: #22c55e; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QFrame#historyCardSessions, QFrame#historyCardReception, QFrame#historyCardAvg, QFrame#historyCardTime {
            background: #ffffff; border: 1px solid #e5e6eb; border-radius: 8px;
        }
        QLabel#historyCardLabel { color: #8a8b90; font-size: 12px; }
        QLabel#historyCardValue { color: #1d1d1f; font-size: 22px; font-weight: bold; }
        QLabel#historyCardSub { color: #8a8b90; font-size: 12px; }
        QFrame#historyFilterPanel {
            background: #f5f7fa; border-radius: 8px; border: 1px solid #e5e6eb;
        }
        QComboBox#historyComboRobot, QComboBox#historyComboStatus {
            background: #ffffff; border: 1px solid #e5e6eb; border-radius: 4px;
            padding: 6px 12px; min-height: 20px; color: #1d1d1f;
        }
        QLineEdit#historyDateEdit, QLineEdit#historySessionSearch {
            background: #ffffff; border: 1px solid #e5e6eb; border-radius: 4px;
            padding: 8px 12px; color: #1d1d1f;
        }
        QLabel#historyToLabel { color: #8a8b90; font-size: 12px; }
        QLabel#historyCountLabel { color: #8a8b90; font-size: 12px; }
        QLabel#historyRealtimeLabel { color: #3b82f6; font-size: 12px; }
        QLabel#historyRecordTitle { color: #1d1d1f; font-size: 14px; font-weight: bold; }
        QLabel#historySortLabel { color: #8a8b90; font-size: 12px; }
        QFrame#historyEmptyPanel {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #f8fafc, stop:1 #eff6ff);
            border: 1px solid #e5e6eb; border-radius: 8px;
        }
        QFrame#historyEmptyIconWrap { background: #e0f2fe; border-radius: 40px; }
        QLabel#historyEmptyTitle { color: #1d1d1f; font-size: 16px; font-weight: bold; }
        QLabel#historyEmptySub { color: #8a8b90; font-size: 12px; }
    )QSS");
}

QString ApplyStyle::backupPageStyle()
{
    return QStringLiteral(R"QSS(
        /* 数据备份页 */
        #backupTipPanel {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #ffedd5, stop:1 #fecdd3);
            border-radius: 8px;
            border: none;
        }

        #backupTipLabel {
            color: #666666;
            font-size: 14px;
            font-weight: 500;
        }

        #backupCard {
            background-color: white;
            border-radius: 8px;
            border: 1px solid #e5e6eb;
        }

        #backupCardLabel {
            color: #666666;
            font-size: 12px;
            margin-top: 8px;
        }

        #backupCardValue {
            color: #1d1d1f;
            font-weight: bold;
            font-size: 18px;
            margin-top: 4px;
        }

        #backupBtnCreate {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #ff7d00, stop:1 #ff4d4f);
            color: white;
            border: none;
            border-radius: 8px;
            font-weight: bold;
            font-size: 14px;
            min-width: 120px;
        }

        #backupBtnCreate:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #ff6b00, stop:1 #ff3d3f);
        }

        #backupBtnCreate:pressed {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #ff5a00, stop:1 #ff2d2f);
        }

        #backupBtnImport, #backupBtnRefresh {
            background-color: white;
            color: #ff4d4f;
            border: 1px solid #ff4d4f;
            border-radius: 8px;
            font-weight: bold;
            font-size: 14px;
            min-width: 120px;
        }

        #backupBtnImport:hover, #backupBtnRefresh:hover {
            background-color: #fff5f5;
        }

        #backupBtnImport:pressed, #backupBtnRefresh:pressed {
            background-color: #ffe5e5;
        }

        #backupMgmtTitle {
            color: #1d1d1f;
            font-weight: bold;
            font-size: 14px;
        }

        #backupViewBtn {
            border: 1px solid #e5e6eb;
            border-radius: 16px;
            background-color: white;
        }

        #backupViewBtn:hover {
            background-color: #f5f7fa;
        }

        #backupViewBtn:checked {
            background-color: #ff4d4f;
            border-color: #ff4d4f;
        }

        #backupViewBtn:checked > QIcon {
            color: white;
        }

        #backupEmptyPanel {
            background-color: #fffaf5;
            border-radius: 8px;
            border: 1px solid #ffedd5;
        }

        #backupEmptyIconWrap {
            background-color: #ffedd5;
            border-radius: 40px;
            border: none;
        }

        #backupEmptyTitle {
            color: #1d1d1f;
            font-weight: bold;
            font-size: 16px;
        }

        #backupEmptySub {
            color: #8a8b90;
            font-size: 12px;
        }

        #backupBtnCreateNow {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #ff7d00, stop:1 #ff4d4f);
            color: white;
            border: none;
            border-radius: 8px;
            font-weight: bold;
            font-size: 14px;
            min-width: 140px;
        }

        #backupBtnCreateNow:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #ff6b00, stop:1 #ff3d3f);
        }

        #backupBtnCreateNow:pressed {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #ff5a00, stop:1 #ff2d2f);
        }
    )QSS");
}

QString ApplyStyle::logPageStyle()
{
    return QStringLiteral(R"QSS(
        /* 日志管理页 */
        #logBannerPanel {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #6366f1, stop:1 #8b5cf6);
            border-radius: 8px;
            border: none;
        }

        #logBannerTitle {
            color: #ffffff;
            font-size: 16px;
            font-weight: bold;
        }

        #logBannerDesc {
            color: #ffffff;
            font-size: 12px;
            opacity: 0.9;
        }

        #logBannerBtnRefresh {
            background-color: rgba(255, 255, 255, 0.2);
            color: #ffffff;
            border: 1px solid rgba(255, 255, 255, 0.3);
            border-radius: 4px;
            font-size: 13px;
        }

        #logBannerBtnRefresh:hover {
            background-color: rgba(255, 255, 255, 0.3);
        }

        #logBannerBtnExport {
            background-color: #ffffff;
            color: #8b5cf6;
            border: none;
            border-radius: 4px;
            font-size: 13px;
        }

        #logBannerBtnExport:hover {
            background-color: #f5f5f5;
        }

        #logCard {
            background-color: white;
            border-radius: 8px;
            border: 1px solid #e5e6eb;
        }

        #logCardTitle {
            color: #8a8b90;
            font-size: 12px;
            margin-top: 8px;
        }

        #logCardValue {
            color: #1d1d1f;
            font-weight: bold;
            font-size: 22px;
            margin-top: 4px;
        }

        #logProgressRed {
            border: none;
            background-color: #f5f7fa;
            border-radius: 2px;
            margin-top: 8px;
        }

        #logProgressRed::chunk {
            background-color: #f56c6c;
            border-radius: 2px;
        }

        #logProgressBlue {
            border: none;
            background-color: #f5f7fa;
            border-radius: 2px;
            margin-top: 8px;
        }

        #logProgressBlue::chunk {
            background-color: #3b82f6;
            border-radius: 2px;
        }

        #logProgressGreen {
            border: none;
            background-color: #f5f7fa;
            border-radius: 2px;
            margin-top: 8px;
        }

        #logProgressGreen::chunk {
            background-color: #00b42a;
            border-radius: 2px;
        }

        #logProgressPurple {
            border: none;
            background-color: #f5f7fa;
            border-radius: 2px;
            margin-top: 8px;
        }

        #logProgressPurple::chunk {
            background-color: #8b5cf6;
            border-radius: 2px;
        }

        #logModuleTitle {
            color: #1d1d1f;
            font-weight: bold;
            font-size: 14px;
        }

        #logFilterPanel {
            background-color: #f3f4f6;
            border-radius: 8px;
            border: 1px solid #e5e6eb;
        }

        #logTimeStart, #logTimeEnd, #logSearch {
            background-color: #ffffff;
            border: 1px solid #e5e6eb;
            border-radius: 4px;
            padding: 8px 12px;
            color: #1d1d1f;
            font-size: 13px;
        }

        #logToLabel {
            color: #8a8b90;
            font-size: 12px;
        }

        #logTypeCombo, #logResultCombo {
            background-color: #ffffff;
            border: 1px solid #e5e6eb;
            border-radius: 4px;
            padding: 6px 12px;
            min-height: 20px;
            color: #1d1d1f;
            font-size: 13px;
        }

        #logBtnQuery {
            background-color: #3b82f6;
            color: #ffffff;
            border: none;
            border-radius: 4px;
            font-size: 13px;
            padding: 0 16px;
        }

        #logBtnQuery:hover {
            background-color: #2563eb;
        }

        #logBtnReset {
            background-color: #f3f4f6;
            color: #1d1d1f;
            border: 1px solid #e5e6eb;
            border-radius: 4px;
            font-size: 13px;
            padding: 0 16px;
        }

        #logBtnReset:hover {
            background-color: #e5e7eb;
        }

        #logTable {
            background-color: #ffffff;
            border: 1px solid #e5e6eb;
            border-radius: 8px;
            gridline-color: #e5e6eb;
            alternate-background-color: #f9fafb;
        }

        #logTable::item {
            color: #1d1d1f;
            padding: 10px 8px;
            font-size: 13px;
        }

        #logTable::item:selected {
            background-color: #e6f4ff;
            color: #1d1d1f;
        }

        QHeaderView::section {
            background-color: #f8fafc;
            color: #8a8b90;
            font-size: 12px;
            font-weight: 500;
            padding: 12px 8px;
            border: none;
            border-bottom: 1px solid #e5e6eb;
        }

        #logPaginationPanel {
            background-color: #ffffff;
            border: 1px solid #e5e6eb;
            border-radius: 8px;
        }

        #logPageInfo {
            color: #8a8b90;
            font-size: 12px;
        }

        #logPageSizeLabel, #logGotoLabel, #logPageLabel {
            color: #8a8b90;
            font-size: 12px;
        }

        #logPageSizeCombo {
            background-color: #ffffff;
            border: 1px solid #e5e6eb;
            border-radius: 4px;
            padding: 6px 12px;
            min-height: 20px;
            color: #1d1d1f;
            font-size: 13px;
        }

        #logBtnPrev, #logBtnNext {
            background-color: #f3f4f6;
            color: #1d1d1f;
            border: 1px solid #e5e6eb;
            border-radius: 4px;
            font-size: 13px;
        }

        #logBtnPrev:hover, #logBtnNext:hover {
            background-color: #e5e7eb;
        }

        #logBtnPage {
            background-color: #ffffff;
            color: #1d1d1f;
            border: 1px solid #e5e6eb;
            border-radius: 4px;
            font-size: 13px;
        }

        #logBtnPage:checked {
            background-color: #3b82f6;
            color: #ffffff;
            border-color: #3b82f6;
        }

        #logGotoInput {
            background-color: #ffffff;
            border: 1px solid #e5e6eb;
            border-radius: 4px;
            padding: 6px;
            color: #1d1d1f;
            font-size: 13px;
        }
    )QSS");
}

QString ApplyStyle::settingsStyle()
{
    return QStringLiteral(R"QSS(
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
    )QSS");
}

QString ApplyStyle::settingsLeftNavStyle()
{
    const int iconSize = 24;
    return QString(R"(
        QListWidget {
            background-color: transparent;
            border: none;
            outline: none;
        }
        QListWidget::item {
            height: %1px;               /* 增加列表项高度 */
            padding-left: 18px;         /* 增加左边内边距，增加图标与文字间距 */
            padding-right: 12px;
            padding-top: 10px;          /* 增加上下内边距 */
            padding-bottom: 10px;
            margin: 2px 8px;            /* 增加项之间的间距 */
            border-radius: 6px;
            color: white;             /* 设置文字颜色 */
            font-size: 11pt;            /* 再次确保字体大小 */
        }
        QListWidget::item:selected {
            background-color: rgba(0, 120, 212, 0.1);
            color: #0078d4;
            font-weight: 500;
        }
        QListWidget::item:hover:!selected {
            background-color: rgba(0, 0, 0, 0.05);
        }
        QListWidget::item:disabled {
            color: #999999;
        }
    )").arg(iconSize + 20);
}
