#ifndef ROBOTASSISTANTWIDGET_H
#define ROBOTASSISTANTWIDGET_H

#include <QJsonArray>
#include <QList>
#include <QString>
#include <QVariant>
#include <QPixmap>
#include <QWidget>
#include "../utils/applystyle.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTabWidget;
class QComboBox;
class QNetworkAccessManager;
class OpenAiCompatClient;
class QVBoxLayout;

struct RobotChatTurn {
    QString role;
    QString content;
};

class RobotAssistantWidget : public QWidget
{
    Q_OBJECT
public:
    /** loginUsername：登录名，用于查询 UserDao；气泡上展示个人信息里的昵称与头像。 */
    explicit RobotAssistantWidget(const QString& loginUsername, QWidget* parent = nullptr);
    void applyTheme(ApplyStyle::MainWindowTheme theme);
    /** 个人信息修改后刷新己方气泡用的昵称与头像（与聚合会话一致）。 */
    void refreshLocalUserProfile();

private slots:
    void onSaveSettings();
    void onTestConnection();
    void onSendMessage();
    void onClearChat();
    void onClientDelta(const QString& delta);
    void onClientCompleted();
    void onClientFailed(const QString& reason);

private:
    void loadSettings();
    void saveSettings();
    /** 按当前设置中的 Base URL / 模型解析 model_key，绑定 session 并从库恢复气泡。 */
    void rebindSessionFromCurrentConfig();
    QJsonArray buildMessagesForRequest() const;
    void setBusy(bool busy);
    void appendUserBubble(const QString& text);
    QLabel* appendAssistantBubble();
    void clearChatLayout();
    void scrollToBottom();
    /** 与聚合接待 scheduleScrollChatToBottom 一致（新气泡、加载历史）。 */
    void scheduleScrollChatToBottom();
    /** 流式改字时无 100ms 等待，仅刷新几何后滚底，避免跟字滞后。 */
    void nudgeScrollAfterContentChange();
    void loadSelfBubbleIdentity();
    void populateModelPresetCombo();
    QVariantMap currentPresetData() const;
    bool currentPresetAvailable() const;
    void applySendButtonPolicy();

    static QString defaultSystemPrompt();

    QTabWidget* m_tabs = nullptr;
    QComboBox* m_modelPresetCombo = nullptr;
    QLineEdit* m_baseUrlEdit = nullptr;
    QLineEdit* m_modelEdit = nullptr;
    QLineEdit* m_apiKeyEdit = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_testBtn = nullptr;
    QLabel* m_privacyLabel = nullptr;

    QWidget* m_chatInner = nullptr;
    QVBoxLayout* m_chatLayout = nullptr;
    QScrollArea* m_scroll = nullptr;
    QPlainTextEdit* m_input = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QLabel* m_statusLabel = nullptr;

    QNetworkAccessManager* m_nam = nullptr;
    OpenAiCompatClient* m_client = nullptr;

    QList<RobotChatTurn> m_history;
    QLabel* m_streamingLabel = nullptr;
    QString m_accumulatedAssistant;
    ApplyStyle::MainWindowTheme m_theme = ApplyStyle::MainWindowTheme::Default;
    bool m_busy = false;
    QString m_loginUsername;
    QString m_selfDisplayName;
    QPixmap m_selfAvatarPixmap;

    int m_userId = 0;
    int m_sessionId = -1;
    QString m_boundModelKey;
};

#endif
