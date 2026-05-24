#ifndef WECHATWORKBENCHDIALOG_H
#define WECHATWORKBENCHDIALOG_H

#include <QJsonArray>
#include <QJsonObject>
#include <QDialog>
#include "../utils/applystyle.h"

class QListWidget;
class QLabel;
class QPushButton;
class QToolButton;
class QPlainTextEdit;
class QComboBox;
class QListWidgetItem;
class QWidget;
class QShowEvent;
class QCloseEvent;
class QEvent;
class WeChatWorkbenchService;

class WeChatWorkbenchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WeChatWorkbenchDialog(QWidget* parent = nullptr);
    void applyTheme(ApplyStyle::MainWindowTheme theme);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onPinTopClicked();
    void onEnvHintCloseClicked();
    void onRefreshSessionsClicked();
    void onRefreshMessagesClicked();
    void onGenerateSuggestionClicked();
    void onSendClicked();
    void onScriptServiceControlClicked();
    void onSessionItemActivated(QListWidgetItem* item);
    void onCommandSucceeded(int requestId, const QString& cmd, const QJsonObject& data);
    void onCommandFailed(int requestId, const QString& cmd, const QString& reason);
    void onAiSuggestionStarted();
    void onAiSuggestionDelta(const QString& delta);
    void onAiSuggestionCompleted(const QString& text);
    void onAiSuggestionFailed(const QString& reason);
    void appendProcessLog(const QString& text);

private:
    void setupUi();
    void refreshAll();
    void updateStatusBanner(const QString& text, bool isError = false);
    void refreshConversationHeader(const QString& metaText = QString());
    void populateSessions(const QJsonObject& data);
    void populateMessages(const QJsonObject& data);
    QString currentSessionName() const;
    QString currentPresetKey() const;
    void setControlsEnabled(bool enabled);
    void updatePinTopButtonUi();
    void updateScriptServiceButtonUi();
    void applyAlwaysOnTop(bool on);

    WeChatWorkbenchService* m_service = nullptr;
    ApplyStyle::MainWindowTheme m_theme = ApplyStyle::MainWindowTheme::Default;
    bool m_firstShow = true;
    bool m_alwaysOnTop = false;
    bool m_updatingSessionList = false;
    QString m_currentConversationName;
    QJsonArray m_currentMessages;

    QLabel* m_statusLabel = nullptr;
    QWidget* m_envHintContainer = nullptr;
    QLabel* m_envHintLabel = nullptr;
    QToolButton* m_envHintCloseBtn = nullptr;
    QToolButton* m_pinTopBtn = nullptr;
    QLabel* m_chatTitleLabel = nullptr;
    QLabel* m_chatMetaLabel = nullptr;
    QListWidget* m_sessionList = nullptr;
    QListWidget* m_messageList = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QToolButton* m_refreshSessionsBtn = nullptr;
    QPushButton* m_refreshMessagesBtn = nullptr;
    QToolButton* m_scriptServiceBtn = nullptr;
    QPushButton* m_generateSuggestionBtn = nullptr;
    QPlainTextEdit* m_replyEdit = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QPlainTextEdit* m_logEdit = nullptr;
};

#endif // WECHATWORKBENCHDIALOG_H
