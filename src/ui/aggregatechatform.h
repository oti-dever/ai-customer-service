#ifndef AGGREGATECHATFORM_H
#define AGGREGATECHATFORM_H

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include "../core/types.h"

class ConversationManager;

class AggregateChatForm : public QMainWindow
{
    Q_OBJECT
public:
    explicit AggregateChatForm(QWidget* parent = nullptr);
    ~AggregateChatForm();

private:
    void setupUI();
    void setupStyles();
    void connectSignals();

    QWidget* buildLeftPanel();
    QWidget* buildCenterPanel();
    QWidget* buildRightPanel();

    void refreshConversationList();
    void showConversation(int conversationId);
    void appendMessageBubble(const MessageRecord& msg);
    void scrollToBottom();
    void updateCustomerInfo(const ConversationInfo& conv);
    void showCenterEmptyState();
    void showRightEmptyState();

    QWidget* createConversationItem(const ConversationInfo& conv);
    QWidget* createBubble(const QString& text, const QString& sender,
                          const QDateTime& time, bool isOutgoing);

private slots:
    void onTabPendingClicked();
    void onTabAllClicked();
    void onConversationItemClicked(QListWidgetItem* item);
    void onSendClicked();
    void onSimulateClicked();

    void onConversationListChanged();
    void onNewMessage(int conversationId, const MessageRecord& msg);
    void onSentOk(int conversationId, const MessageRecord& msg);

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
    QPushButton* m_btnSend = nullptr;
    QLabel* m_chatHeader = nullptr;
    QWidget* m_rightEmptyState = nullptr;
    QWidget* m_customerDetail = nullptr;
    QLabel* m_customerName = nullptr;
    QLabel* m_customerPlatform = nullptr;
    QLabel* m_customerStatus = nullptr;

    bool m_currentTabIsPending = true;
    int m_currentConvId = -1;
};

#endif // AGGREGATECHATFORM_H
