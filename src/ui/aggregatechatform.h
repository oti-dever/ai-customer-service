#ifndef AGGREGATECHATFORM_H
#define AGGREGATECHATFORM_H

#include <QComboBox>
#include <QPoint>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QWidget>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QPixmap>
#include <QString>
#include <QVBoxLayout>
#include "../core/types.h"
#include "../utils/applystyle.h"

class ConversationManager;
class QNetworkAccessManager;
class OpenAiCompatClient;

class AggregateChatForm : public QWidget
{
    Q_OBJECT
public:
    explicit AggregateChatForm(const QString& loginUsername, QWidget* parent = nullptr);
    ~AggregateChatForm();
    void applyTheme(ApplyStyle::MainWindowTheme theme);
    /** 个人信息等变更后刷新己方气泡用的昵称与头像缓存，并可重绘当前会话。 */
    void refreshLocalUserProfile();

private:
    void setupUI();
    void setupStyles();
    void connectSignals();

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
    void setAggregateAiBusy(bool busy);
    void abortAggregateAiRequest();

    QWidget* createConversationItem(const ConversationInfo& conv);
    QWidget* createBubble(const MessageRecord& msg);
    QWidget* createDateSeparator(const QDate& date);
    void loadSelfBubbleIdentity();

private slots:
    void onTabPendingClicked();
    void onTabAllClicked();
    void onConversationItemClicked(QListWidgetItem* item);
    void onSendClicked();
    void onSimulateClicked();
    void onModeComboChanged();
    void onGenerateAiDraftClicked();
    void onAggregateAiStreamDelta(const QString& delta);
    void onAggregateAiCompleted();
    void onAggregateAiFailed(const QString& reason);

    void onConversationListChanged();
    void onNewMessage(int conversationId, const MessageRecord& msg);
    void onSentOk(int conversationId, const MessageRecord& msg);
    void onClearSendTimeline();
    void pollSendTimeline();
    void onConversationListContextMenu(const QPoint& pos);

private:
    QWidget* m_leftPanel = nullptr;
    QWidget* m_centerPanel = nullptr;
    QWidget* m_rightPanel = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QPushButton* m_btnPending = nullptr;
    QPushButton* m_btnAll = nullptr;
    QPushButton* m_btnSimulate = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_conversationList = nullptr;
    QStackedWidget* m_leftStack = nullptr;
    QStackedWidget* m_centerStack = nullptr;
    QStackedWidget* m_rightStack = nullptr;
    QWidget* m_centerEmptyState = nullptr;
    QWidget* m_chatArea = nullptr;
    QScrollArea* m_messageScroll = nullptr;
    QVBoxLayout* m_messageLayout = nullptr;
    QWidget* m_messageContainer = nullptr;
    QPlainTextEdit* m_inputEdit = nullptr;
    QPushButton* m_btnAiGenerate = nullptr;
    QPushButton* m_btnSend = nullptr;
    QNetworkAccessManager* m_aggregateAiNam = nullptr;
    OpenAiCompatClient* m_aggregateAiClient = nullptr;
    bool m_aggregateAiGenerating = false;
    QString m_aggregateAiBaseline;
    QString m_aggregateAiAccumulated;
    QLabel* m_chatHeader = nullptr;
    QWidget* m_rightEmptyState = nullptr;
    QWidget* m_customerDetail = nullptr;
    QLabel* m_customerName = nullptr;
    QLabel* m_customerPlatform = nullptr;
    QLabel* m_customerStatus = nullptr;
    QLabel* m_sendTimelineLabel = nullptr;
    QPlainTextEdit* m_sendTimeline = nullptr;
    QPushButton* m_btnClearSendTimeline = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTimer* m_messageRefreshTimer = nullptr;
    QTimer* m_sendTimelineTimer = nullptr;
    qint64 m_sendTimelineBaselineId = 0;

    bool m_currentTabIsPending = true;
    int m_currentConvId = -1;
    /** 发送成功且最后一条为 out 时暂留在「待处理」，切换离开该会话后清除（见 refreshConversationList）。 */
    int m_pendingStickyConvId = -1;
    QDate m_lastBubbleDate;
    QString m_currentMessageSignature;

    QString m_loginUsername;
    QString m_selfDisplayName;
    QPixmap m_selfAvatarPixmap;
};

#endif // AGGREGATECHATFORM_H
