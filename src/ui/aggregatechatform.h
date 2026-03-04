#ifndef AGGREGATECHATFORM_H
#define AGGREGATECHATFORM_H

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>

class AggregateChatForm : public QMainWindow
{
    Q_OBJECT
public:
    explicit AggregateChatForm(QWidget *parent = nullptr);
    ~AggregateChatForm();
private:
    void setupUI();
    void setupStyles();
    QWidget* buildLeftPanel();
    QWidget* buildCenterPanel();
    QWidget* buildRightPanel();
    void showConversationEmptyState();
    void showCustomerEmptyState();

private slots:
    void onTabPendingClicked();
    void onTabAllClicked();
    void onConversationItemClicked(int row);

private:
    QWidget* m_leftPanel = nullptr;
    QWidget* m_centerPanel = nullptr;
    QWidget* m_rightPanel = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QPushButton* m_btnPending = nullptr;
    QPushButton* m_btnAll = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_conversationList = nullptr;
    QWidget* m_centerEmptyState = nullptr;
    QWidget* m_chatArea = nullptr;
    QPlainTextEdit* m_inputEdit = nullptr;
    QPushButton* m_btnSend = nullptr;
    QWidget* m_rightEmptyState = nullptr;
    QWidget* m_customerDetail = nullptr;
    bool m_currentTabIsPending = true;
};
#endif // AGGREGATECHATFORM_H
