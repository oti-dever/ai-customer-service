#ifndef AGGREGATECHATFORM_H
#define AGGREGATECHATFORM_H

#include <QCheckBox>
#include <QPoint>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QWidget>
#include <QMap>
#include <QSet>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMenu>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QPixmap>
#include <QString>
#include <QEvent>
#include <QElapsedTimer>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include "../core/types.h"
#include "../ipc/ipctypes.h"
#include "../models/unifiedmodels.h"
#include "../utils/applystyle.h"

class ConversationManager;
class ConversationAppService;
class AiChatAppService;
class IAiStreamingSession;
class ConversationListModel;
class MessageListModel;
class QJsonObject;
class QStyledItemDelegate;
class QToolButton;
class QAction;
class QButtonGroup;
class QSplitter;
class QModelIndex;
class QHBoxLayout;
class QGridLayout;
class QMimeData;
class QResizeEvent;

/** 左侧平台工具条：与库中 `conversations.platform` 一致（如 qianniu、pdd_web、douyin、wechat）。 */
enum class AggregatePlatformFilter { All = 0, Qianniu = 1, Pdd = 2, Doudian = 3, Wechat = 4 };
enum class AggregateConversationTab { All = 0, Pending = 1, Replied = 2 };
enum class AggregateAdaptiveLayoutMode { Unknown = -1, Wide = 0, Medium = 1, Compact = 2 };

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
    void resizeEvent(QResizeEvent* event) override;
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
    void reloadFromLocalCache();
    /** 兼容旧命名；主路径请使用 reloadFromLocalCache()。 */
    void reloadFromDatabase();
    void applyCacheSnapshotToLocalCache(const QJsonObject& snapshot);
    QVector<MessageRecord> messagesForDisplay(int conversationId) const;
    void renderConversationListFromModel();
    void showConversation(int conversationId);
    void renderConversationMessages(const QVector<MessageRecord>& messages);
    void renderConversationMessagesFromModel();
    void appendMessageBubble(const MessageRecord& msg);
    QString buildMessageSignature(const QVector<MessageRecord>& messages) const;
    void refreshVisibleConversationMessages();
    void scrollToBottom();
    /** 在布局完成后再滚到底部（切换会话、追加消息等场景） */
    void scheduleScrollChatToBottom();
    void persistCurrentDraft();
    void restoreDraftForConversation(int conversationId);
    bool handleComposeMimeData(const QMimeData* mimeData);
    bool addComposeFileAttachment(const QString& path);
    void refreshComposeAttachments();
    void clearComposeAttachments();
    void restoreLastSelectedConversation();
    void updateCustomerInfo(const ConversationInfo& conv);
    void resetSendTimelineForConversation();
    void showCenterEmptyState();
    void showRightEmptyState();
    void showStatusMessage(const QString& text, int timeoutMs);
    void openMessageMedia(const MessageRecord& msg);
    bool applyServiceConversationMutation(const ConversationInfo& conv, bool deleteConversation);
    void updateAggregateAiControlsVisibility();
    void refreshAggregateAiModelButtonUi();
    bool isAggregateAutoReplyEnabled() const;
    void setAggregateAutoReplyEnabled(bool enabled);
    void refreshAutoReplyToggleButtonUi();
    void setAggregateAiBusy(bool busy);
    QStringList selectedPlatformListenTargets() const;
    void refreshPlatformListenStateFromService();
    void backfillFromPythonService();
    void schedulePythonServiceBackfill(int delayMs = 250);
    void setPlatformListenControlsEnabled(bool enabled);
    void updatePlatformListenStatusLabel();
    void refreshPythonServiceButtonUi();
    void abortAggregateAiRequest();
    void abortAutoReplyRequest();
    void clearStreamingSession(IAiStreamingSession*& session);
    /** 自动回复开启后，在满足条件时尝试生成并发送（T1 切换会话 / T2 当前会话新入站）。 */
    void tryAggregateAutoReply(int conversationId, const QString& triggerTag);
    void relayoutChatInputOverlay();
    void updateMessageListBottomReserve(int overlayBottomPx);
    void syncConversationItemVisualState();
    void syncSolidBackgrounds();
    void refreshRightBarModelDisplay();
    void refreshRightBarMetrics();
    void refreshCustomerProfilePanel();
    void setCustomerProfileBusy(bool busy);
    QJsonObject parseCustomerProfileJson(const QString& text) const;
    void setConversationTab(AggregateConversationTab tab);
    /** 折叠/展开「处理动态」时切换竖直 stretch，使折叠时主内容区顶对齐、不分散大块空白。 */
    void updateRightBarSendSectionStretch();
    void setChatHeaderTitle(const QString& title);
    void scheduleAdaptiveRelayout(bool fromUserSplitter = false);
    void runScheduledAdaptiveRelayout();
    void scheduleChatInputRelayout();
    void updateComposeInputHeight();
    AggregateAdaptiveLayoutMode desiredAdaptiveLayoutMode() const;
    void updateAdaptiveConversationLayout(bool fromUserSplitter = false);
    void setConversationPanelHidden(bool hidden);
    void updateRightBarAdaptiveLayout();
    void updateCompactHeaderControls();
    void setRightBarHidden(bool hidden);
    void openModelPickerSheet();
    void closeModelPickerSheet();
    void startModelSheetWidthAnimation(int fromWidth, int toWidth, bool hideOverlayOnFinish);

    QWidget* createBubble(const MessageRecord& msg);
    QWidget* createDateSeparator(const QDate& date);
    void loadSelfBubbleIdentity();

private slots:
    void onTabAllClicked();
    void onTabPendingClicked();
    void onTabRepliedClicked();
    void onPlatformFilterButtonIdClicked(int id);
    void onSimulateMessageClicked();
    void onConversationIndexClicked(const QModelIndex& index);
    void onConversationItemClicked(QListWidgetItem* item);
    void onSendClicked();
    void onAutoReplyToggleClicked();
    void onStartPlatformListeningClicked();
    void onStopPlatformListeningClicked();
    void onPythonServiceButtonClicked();
    void onPythonServiceButtonContextMenu(const QPoint& pos);
    void onGenerateAiDraftClicked();
    void onAggregateAiModelMenuTriggered(QAction* action);
    void onIpcAiSuggestionReceived(const Ipc::AiSuggestionResponse& response);
    void onIpcRequestFailed(const QString& requestId, const QString& reason);
    void onAggregateAiStreamDelta(const QString& delta);
    void onAggregateAiCompleted();
    void onAggregateAiFailed(const QString& reason);
    void onOrganizeCustomerProfileClicked();
    void onCustomerProfileStreamDelta(const QString& delta);
    void onCustomerProfileCompleted();
    void onCustomerProfileFailed(const QString& reason);
    void onAutoReplyStreamDelta(const QString& delta);
    void onAutoReplyCompleted();
    void onAutoReplyFailed(const QString& reason);

    void onConversationListChanged();
    void onUnifiedMessageReceived(int conversationId, const Models::Message& message);
    void onUnifiedConversationUpdated(const Models::Conversation& conversation);
    void onConversationMessagesCleared(int conversationId);
    void onConversationDeleted(int conversationId);
    void onNewMessage(int conversationId, const MessageRecord& msg);
    void onSentOk(int conversationId, const MessageRecord& msg);
    void onClearSendTimeline();
    void pollSendTimeline();
    void onConversationListContextMenu(const QPoint& pos);
    void onModelPickerListItem(QListWidgetItem* item);
    void onModelPickerBackClicked();
    void onMessageStatusChanged(int conversationId, int messageId, Models::MessageStatus newStatus, const QString& errorReason);

private:
    QWidget* m_leftToolBar = nullptr;
    QSplitter* m_hSplitter = nullptr;
    QButtonGroup* m_platformButtonGroup = nullptr;
    AggregatePlatformFilter m_platformFilter = AggregatePlatformFilter::All;
    QWidget* m_leftPanel = nullptr;
    QWidget* m_centerPanel = nullptr;
    QWidget* m_rightPanel = nullptr;
    QLabel* m_platformSectionTitle = nullptr;
    QCheckBox* m_chkListenWechat = nullptr;
    QCheckBox* m_chkListenQianniu = nullptr;
    QPushButton* m_btnStartPlatformListening = nullptr;
    QPushButton* m_btnStopPlatformListening = nullptr;
    QLabel* m_platformListenStatusLabel = nullptr;
    bool m_pythonServiceAvailable = false;
    bool m_pythonBackfillInProgress = false;
    QSet<QString> m_registeredListenPlatforms;
    QSet<QString> m_serviceListeningPlatforms;
    QPushButton* m_btnAll = nullptr;
    QPushButton* m_btnPending = nullptr;
    QPushButton* m_btnReplied = nullptr;
    QPushButton* m_btnAutoReplyToggle = nullptr;
    QToolButton* m_btnPythonService = nullptr;
    QToolButton* m_btnSimulateMessage = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QListView* m_conversationList = nullptr;
    QStackedWidget* m_leftStack = nullptr;
    QStackedWidget* m_centerStack = nullptr;
    QStackedWidget* m_rightStack = nullptr;
    QWidget* m_centerEmptyState = nullptr;
    QWidget* m_chatArea = nullptr;
    /** 覆盖在消息列表上的容器：底部为半透明输入条，消息可滚至其下方并透出。 */
    QWidget* m_chatInputOverlayHost = nullptr;
    QWidget* m_chatInputPanel = nullptr;
    QListView* m_messageView = nullptr;
    QPlainTextEdit* m_inputEdit = nullptr;
    QScrollArea* m_composeAttachmentsScroll = nullptr;
    QWidget* m_composeAttachmentsWidget = nullptr;
    QHBoxLayout* m_composeAttachmentsLayout = nullptr;
    QToolButton* m_btnAiModelPick = nullptr;
    QMenu* m_aggregateAiModelMenu = nullptr;
    QString m_aggregateAiSessionModelKey;
    QPushButton* m_btnAiGenerate = nullptr;
    QPushButton* m_btnSend = nullptr;
    ConversationListModel* m_conversationListModel = nullptr;
    MessageListModel* m_messageListModel = nullptr;
    QStyledItemDelegate* m_messageItemDelegate = nullptr;
    ConversationAppService* m_conversationService = nullptr;
    AiChatAppService* m_aiChatService = nullptr;
    IAiStreamingSession* m_aggregateAiSession = nullptr;
    /** 与 AI 辅助分离，避免与「生成本条回复」并发共用一个 client。 */
    IAiStreamingSession* m_autoReplySession = nullptr;
    IAiStreamingSession* m_customerProfileSession = nullptr;
    bool m_aggregateAiGenerating = false;
    bool m_autoReplyBusy = false;
    int m_autoReplyTargetConvId = -1;
    QString m_autoReplyAccumulated;
    QString m_aggregateAiIpcRequestId;
    QString m_aggregateAiBaseline;
    QString m_aggregateAiAccumulated;
    qint64 m_aggregateAiRequestEventId = 0;
    QElapsedTimer m_aggregateAiRequestTimer;
    int m_aggregateAiFirstTokenMs = 0;
    qint64 m_autoReplyRequestEventId = 0;
    QElapsedTimer m_autoReplyRequestTimer;
    int m_autoReplyFirstTokenMs = 0;
    bool m_customerProfileBusy = false;
    qint64 m_customerProfileRequestEventId = 0;
    QElapsedTimer m_customerProfileRequestTimer;
    int m_customerProfileFirstTokenMs = 0;
    QString m_customerProfileAccumulated;
    QWidget* m_chatHeaderBar = nullptr;
    QToolButton* m_btnBackToConversationList = nullptr;
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
    QWidget* m_customerProfileSection = nullptr;
    QToolButton* m_customerProfileToggle = nullptr;
    QPushButton* m_btnOrganizeCustomerProfile = nullptr;
    QWidget* m_customerProfileBody = nullptr;
    QLabel* m_customerProfileText = nullptr;
    QPushButton* m_btnModelSwitchRow = nullptr;
    /** 与 aiPresetDefinitions() 顺序一致，仅影响右栏展示，不接真实请求路由。 */
    int m_rightBarDisplayModelIndex = 0;
    QWidget* m_rightBarMetricsWrap = nullptr;
    QGridLayout* m_rightBarMetricsGrid = nullptr;
    QWidget* m_rightBarMetricCards[4] = {};
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
    QTimer* m_draftSaveTimer = nullptr;
    QTimer* m_pythonBackfillTimer = nullptr;
    QTimer* m_adaptiveRelayoutTimer = nullptr;
    QTimer* m_chatInputRelayoutTimer = nullptr;
    qint64 m_sendTimelineBaselineId = 0;
    qint64 m_aiStageTimelineBaselineId = 0;
    bool m_rightBarHidden = false;
    bool m_rightBarAutoHidden = false;
    int m_lastRightBarWidth = 296;
    bool m_leftPanelHidden = false;
    bool m_compactConversationListForced = false;
    int m_lastLeftPanelWidth = 248;
    int m_rightBarMetricColumnCount = 0;
    bool m_pendingAdaptiveRelayoutFromUserSplitter = false;
    AggregateAdaptiveLayoutMode m_adaptiveLayoutMode = AggregateAdaptiveLayoutMode::Unknown;
    bool m_restoringDraft = false;
    QVector<OutgoingMessagePart> m_composeAttachments;
    QMap<int, QVector<OutgoingMessagePart>> m_draftAttachments;

    AggregateConversationTab m_currentTab = AggregateConversationTab::Pending;
    int m_currentConvId = -1;
    /** 发送成功且最后一条为 out 时暂留在「待处理」，切换离开该会话后清除（见 refreshConversationList）。 */
    int m_pendingStickyConvId = -1;
    QDate m_lastBubbleDate;
    QString m_currentMessageSignature;

    QString m_loginUsername;
    QString m_selfDisplayName;
    QPixmap m_selfAvatarPixmap;
    /** 入站消息气泡通用对方头像 */
    QPixmap m_customerDefaultAvatarPixmap;
};

#endif // AGGREGATECHATFORM_H
