#ifndef ROBOTASSISTANTWIDGET_H
#define ROBOTASSISTANTWIDGET_H

#include <QList>
#include <QString>
#include <QVariant>
#include <QPixmap>
#include <QWidget>
#include "../services/ai/aitypes.h"
#include "../utils/applystyle.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QComboBox;
class ConversationAppService;
class AiChatAppService;
class IAiStreamingSession;
class QVBoxLayout;
class QToolButton;

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
    ~RobotAssistantWidget() override;
    void applyTheme(ApplyStyle::MainWindowTheme theme);
    /** 个人信息修改后刷新己方气泡用的昵称与头像（与聚合会话一致）。 */
    void refreshLocalUserProfile();

public slots:
    /** 「AI 客服后台」保存 API 后刷新：本会话会按 QSettings 重新解析线路配置。 */
    void onExternalProviderConfigChanged();

private slots:
    void onSendMessage();
    void onClearChat();
    void onPickPicture();
    void onPickFile();
    void onClientDelta(const QString& delta);
    void onClientCompleted();
    void onClientFailed(const QString& reason);

private:
    /** 按当前线路解析 `sessionModelKey`，绑定会话并从库恢复气泡。 */
    void rebindSessionFromCurrentConfig();
    AiRequest buildAiRequestForConversation() const;
    /** 以磁盘/QSettings 中当前预设为准（对话页无未保存的 URL/Key 编辑区）。 */
    AiProviderConfig currentAiProviderConfig() const;
    void setBusy(bool busy);
    void appendUserBubble(const QString& text);
    void appendUserImageBubble(const QString& absolutePath);
    void appendUserMultimodalDisplay(const QString& absolutePath, const QString& text);
    void appendUserFileBubble(const QString& absolutePath, const QString& displayName, const QString& text);
    QLabel* appendAssistantBubble();
    void clearChatLayout();
    void scrollToBottom();
    /** 与聚合接待 scheduleScrollChatToBottom 一致（新气泡、加载历史）。 */
    void scheduleScrollChatToBottom();
    /** 流式改字时无 100ms 等待，仅刷新几何后滚底，避免跟字滞后。 */
    void nudgeScrollAfterContentChange();
    void loadSelfBubbleIdentity();
    void fillPresetCombo(QComboBox* combo);
    void migrateLegacyAiSettingsToPresets();
    QString sessionModelKeyAtComboIndex(int index) const;
    void onPresetComboIndexChanged(int index);
    QVariantMap currentPresetData() const;
    bool currentPresetAvailable() const;
    void applySendButtonPolicy();
    void applyAttachmentButtonsPolicy();
    void setStatusText(const QString& text);
    void updatePendingAttachmentUi();
    void clearPendingAttachment();
    void clearStreamingSession(IAiStreamingSession*& session);
    /** 正常结束流式：落库、清缓冲、恢复可接收增量标志。 */
    void finishAssistantStreamSuccess(const QString& statusText);

    /** 含当前预设的后端身份说明，供 buildAiRequestForConversation 使用。 */
    QString systemPromptForRequest() const;
    QString assistantDisplayNameForSessionKey(const QString& sessionModelKey) const;
    QString assistantAvatarResourceForSessionKey(const QString& sessionModelKey) const;

    QToolButton* m_pictureBtn = nullptr;
    QToolButton* m_fileBtn = nullptr;
    QWidget* m_pendingAttachmentRow = nullptr;
    QLabel* m_pendingThumbLabel = nullptr;
    QLabel* m_pendingNameLabel = nullptr;
    QPushButton* m_pendingClearBtn = nullptr;
    /** 待发送的本地图片绝对路径（仅豆包多模态使用）。 */
    QString m_pendingImagePath;
    /** 待发送的本地文件（仅豆包 + 方舟 Files API / Responses）。与图片二选一。 */
    QString m_pendingFilePath;

    QComboBox* m_modelPresetCombo = nullptr;

    QWidget* m_chatInner = nullptr;
    QVBoxLayout* m_chatLayout = nullptr;
    QScrollArea* m_scroll = nullptr;
    QPlainTextEdit* m_input = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QLabel* m_statusLabel = nullptr;

    ConversationAppService* m_conversationService = nullptr;
    AiChatAppService* m_aiChatService = nullptr;
    IAiStreamingSession* m_activeSession = nullptr;

    QList<RobotChatTurn> m_history;
    QLabel* m_streamingLabel = nullptr;
    QString m_accumulatedAssistant;
    /** 软上限后已中止网络流，忽略队列中尚未投递的增量，避免写回空缓冲。 */
    bool m_acceptAssistantStreamDeltas = true;
    ApplyStyle::MainWindowTheme m_theme = ApplyStyle::MainWindowTheme::Default;
    bool m_busy = false;
    QString m_loginUsername;
    QString m_selfDisplayName;
    QPixmap m_selfAvatarPixmap;

    int m_userId = 0;
    int m_sessionId = -1;
    QString m_boundModelKey;
    /** 当前线路/预设（与 QSettings 中 `sessionModelKey` 一致）。 */
    QString m_activePresetSessionKey;
};

#endif
