#ifndef AGGREGATECHATFORM_H
#define AGGREGATECHATFORM_H

#include <QComboBox>
#include <QPoint>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QWidget>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMenu>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QPixmap>
#include <QString>
#include <QEvent>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include "../core/types.h"
#include "../utils/applystyle.h"

class ConversationManager;
class ConversationAppService;
class AiChatAppService;
class IAiStreamingSession;
class QToolButton;
class QAction;
class QButtonGroup;
class QSplitter;

/** 左侧平台工具条：与库中 `conversations.platform` 一致（如 qianniu、pdd_web、douyin、wechat_pc）。 */
enum class AggregatePlatformFilter { All = 0, Qianniu = 1, Pdd = 2, Doudian = 3, Wechat = 4 };
enum class AggregateConversationTab { All = 0, Pending = 1, Replied = 2 };

class AggregateChatForm : public QWidget
{
    Q_OBJECT
public:
    explicit AggregateChatForm(const QString& loginUsername, QWidget* parent = nullptr);
    ~AggregateChatForm();
    void applyTheme(ApplyStyle::MainWindowTheme theme);
    /** 个人信息等变更后刷新己方气泡用的昵称与头像缓存，并可重绘当前会话。 */
    void refreshLocalUserProfile();

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUI();
    void setupStyles();
    void connectSignals();

    QWidget* buildLeftToolBar();
    void updatePlatformToolBarButtonIcons();
    void updatePlatformSectionTitle();
    QWidget* buildLeftPanel();
    QWidget* buildCenterPanel();
    QWidget* buildRightPanel();

    void refreshConversationList();
    void showConversation(int conversationId);
    void renderConversationMessages(const QVector<MessageRecord>& messages);
    void appendMessageBubble(const MessageRecord& msg);
    QString buildMessageSignature(const QVector<MessageRecord>& messages) const;
    void refreshVisibleConversationMessages();
    void scrollToBottom();
    /** 在布局完成后再滚到底部（切换会话、追加消息等场景） */
    void scheduleScrollChatToBottom();
    void updateCustomerInfo(const ConversationInfo& conv);
    void resetSendTimelineForConversation();
    void showCenterEmptyState();
    void showRightEmptyState();
    void showStatusMessage(const QString& text, int timeoutMs);
    void updateAggregateAiControlsVisibility();
    void refreshAggregateAiModelButtonUi();
    void setAggregateAiBusy(bool busy);
    void abortAggregateAiRequest();
    void clearStreamingSession(IAiStreamingSession*& session);
    /** 千牛 + AI 自动回复模式下，在满足 §3 条件时尝试生成并发送（T1 切换会话 / T2 当前会话新入站）。 */
    void tryAggregateAutoReply(int conversationId, const QString& triggerTag);
    void relayoutChatInputOverlay();
    void updateMessageListBottomReserve(int overlayBottomPx);
    void syncConversationItemVisualState();
    void syncSolidBackgrounds();
    void refreshRightBarModelDisplay();
    void setConversationTab(AggregateConversationTab tab);
    /** 折叠/展开「查看运行日志」时切换竖直 stretch，使折叠时主内容区顶对齐、不分散大块空白。 */
    void updateRightBarSendSectionStretch();
    void setupRightBarToggleButton();
    void setRightBarHidden(bool hidden);
    void updateRightBarToggleButtonGeometry();
    void updateRightBarToggleButtonVisibility(const QPoint& globalPos);
    void installRightBarToggleEventFilter(QWidget* widget);
    void openModelPickerSheet();
    void closeModelPickerSheet();
    void startModelSheetWidthAnimation(int fromWidth, int toWidth, bool hideOverlayOnFinish);

    QWidget* createConversationItem(const ConversationInfo& conv);
    QWidget* createBubble(const MessageRecord& msg);
    QWidget* createDateSeparator(const QDate& date);
    void loadSelfBubbleIdentity();

private slots:
    void onTabAllClicked();
    void onTabPendingClicked();
    void onTabRepliedClicked();
    void onPlatformFilterButtonIdClicked(int id);
    void onConversationItemClicked(QListWidgetItem* item);
    void onSendClicked();
    void onStopAutoReplyClicked();
    void onModeComboChanged();
    void onGenerateAiDraftClicked();
    void onAggregateAiModelMenuTriggered(QAction* action);
    void onAggregateAiStreamDelta(const QString& delta);
    void onAggregateAiCompleted();
    void onAggregateAiFailed(const QString& reason);
    void onAutoReplyStreamDelta(const QString& delta);
    void onAutoReplyCompleted();
    void onAutoReplyFailed(const QString& reason);

    void onConversationListChanged();
    void onNewMessage(int conversationId, const MessageRecord& msg);
    void onSentOk(int conversationId, const MessageRecord& msg);
    void onClearSendTimeline();
    void pollSendTimeline();
    void onConversationListContextMenu(const QPoint& pos);
    void onModelPickerListItem(QListWidgetItem* item);
    void onModelPickerBackClicked();

private:
    QWidget* m_leftToolBar = nullptr;
    QSplitter* m_hSplitter = nullptr;
    QButtonGroup* m_platformButtonGroup = nullptr;
    AggregatePlatformFilter m_platformFilter = AggregatePlatformFilter::All;
    QWidget* m_leftPanel = nullptr;
    QWidget* m_centerPanel = nullptr;
    QWidget* m_rightPanel = nullptr;
    QLabel* m_platformSectionTitle = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QPushButton* m_btnAll = nullptr;
    QPushButton* m_btnPending = nullptr;
    QPushButton* m_btnReplied = nullptr;
    /** 原「模拟消息」；现为 **一键停止** AI 自动回复（并切人工接待），见《AI自动回复方案设计与实现》。 */
    QPushButton* m_btnStopAutoReply = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_conversationList = nullptr;
    QStackedWidget* m_leftStack = nullptr;
    QStackedWidget* m_centerStack = nullptr;
    QStackedWidget* m_rightStack = nullptr;
    QWidget* m_centerEmptyState = nullptr;
    QWidget* m_chatArea = nullptr;
    /** 覆盖在消息列表上的容器：底部为半透明输入条，消息可滚至其下方并透出。 */
    QWidget* m_chatInputOverlayHost = nullptr;
    QWidget* m_chatInputPanel = nullptr;
    QScrollArea* m_messageScroll = nullptr;
    QVBoxLayout* m_messageLayout = nullptr;
    QWidget* m_messageContainer = nullptr;
    QPlainTextEdit* m_inputEdit = nullptr;
    QToolButton* m_btnAiModelPick = nullptr;
    QMenu* m_aggregateAiModelMenu = nullptr;
    QString m_aggregateAiSessionModelKey;
    QPushButton* m_btnAiGenerate = nullptr;
    QPushButton* m_btnSend = nullptr;
    ConversationAppService* m_conversationService = nullptr;
    AiChatAppService* m_aiChatService = nullptr;
    IAiStreamingSession* m_aggregateAiSession = nullptr;
    /** 与 AI 辅助分离，避免与「生成本条回复」并发共用一个 client。 */
    IAiStreamingSession* m_autoReplySession = nullptr;
    bool m_aggregateAiGenerating = false;
    bool m_autoReplyBusy = false;
    int m_autoReplyTargetConvId = -1;
    QString m_autoReplyAccumulated;
    QString m_aggregateAiBaseline;
    QString m_aggregateAiAccumulated;
    QLabel* m_chatHeader = nullptr;
    QWidget* m_customerDetail = nullptr;
    QScrollArea* m_rightBarScroll = nullptr;
    /** 右栏可滚内容根，需与 sync/QSS 一起固定浅色底，避免跟系统暗色表盘成黑底。 */
    QWidget* m_rightBarScrollContent = nullptr;
    QVBoxLayout* m_rightBarVLayout = nullptr;
    /** 可伸缩底部占位，折叠时消耗竖直富余，避免多段块之间被「撑开」出现中段空白。 */
    QWidget* m_rightBarBottomSpacer = nullptr;
    /** 发送状态标题+输出区的外层，与主列 spacing 解耦、便于收紧标题与文本区间距。 */
    QWidget* m_rightBarSendStatusSection = nullptr;
    QPushButton* m_btnModelSwitchRow = nullptr;
    /** 与 aiPresetDefinitions() 顺序一致，仅影响右栏展示，不接真实请求路由。 */
    int m_rightBarDisplayModelIndex = 0;
    QLabel* m_rightBarModelIconLabel = nullptr;
    QLabel* m_rightBarModelNameLabel = nullptr;
    QWidget* m_rightBarModelStatusDot = nullptr;
    QListWidget* m_modelPickerList = nullptr;
    QWidget* m_modelPickerOverlay = nullptr;
    QWidget* m_modelPickerSheet = nullptr;
    QToolButton* m_btnModelPickerBack = nullptr;
    QVariantAnimation* m_modelSheetWidthAnim = nullptr;
    /** 指标占位格对应 QLabel（数值行），0..3 */
    QLabel* m_rightBarMetricValues[4] = {};
    QToolButton* m_sendTimelineToggle = nullptr;
    QToolButton* m_btnClearSendTimeline = nullptr;
    QWidget* m_sendTimelineBody = nullptr;
    QPlainTextEdit* m_sendTimeline = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTimer* m_messageRefreshTimer = nullptr;
    QTimer* m_sendTimelineTimer = nullptr;
    qint64 m_sendTimelineBaselineId = 0;
    QToolButton* m_rightBarToggleButton = nullptr;
    bool m_rightBarHidden = false;
    int m_lastRightBarWidth = 296;

    AggregateConversationTab m_currentTab = AggregateConversationTab::Pending;
    int m_currentConvId = -1;
    /** 发送成功且最后一条为 out 时暂留在「待处理」，切换离开该会话后清除（见 refreshConversationList）。 */
    int m_pendingStickyConvId = -1;
    QDate m_lastBubbleDate;
    QString m_currentMessageSignature;

    /** 模式下拉：避免程序化 `setCurrentIndex` 触发确认逻辑递归。 */
    bool m_suppressingModeComboChange = false;
    /** 上一次稳定的模式索引（0 人工 / 1 AI辅助 / 2 AI自动回复），用于取消确认后回退。 */
    int m_lastAggregateModeIndex = 0;

    QString m_loginUsername;
    QString m_selfDisplayName;
    QPixmap m_selfAvatarPixmap;
    /** 入站消息气泡通用对方头像 */
    QPixmap m_customerDefaultAvatarPixmap;
};

#endif // AGGREGATECHATFORM_H
