#ifndef ROBOTSANDBOXDIALOG_H
#define ROBOTSANDBOXDIALOG_H

#include "../services/app/aichatappservice.h"

#include <QDialog>
#include <QJsonObject>
#include <QList>

class IAiStreamingSession;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class RobotSandboxDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit RobotSandboxDialog(const QJsonObject& robot, QWidget* parent = nullptr);
    ~RobotSandboxDialog() override;

    struct ImageCandidate {
        QString assetId;
        QString title;
        QString originalFilename;
        QString filePath;
        QString reason;
        QString riskTags;
        double score = 0.0;
        bool shouldAttach = false;
    };

private:
    void buildUi();
    void loadHistory();
    void addTextBubble(const QString& role, const QString& text);
    void addImageBubble(const QString& path, const QJsonObject& metadata);
    void addPendingBubble();
    void removePendingBubble();
    void scrollToBottom();
    void updateImageCandidateSummary(const QString& status);
    void sendCurrentQuestion();
    void clearHistory();
    void setBusy(bool busy, const QString& status = QString());
    QList<KnowledgeSnippetContext> retrieveKnowledge(const QString& query, QString* statusOut);
    QList<ImageCandidate> retrieveImages(const QString& query, QString* statusOut);
    QList<AiConversationTurn> recentTurnsExcludingLatest() const;
    AggregateReplyStrategy replyStrategy() const;
    QString knowledgeQueryForLatest(const QString& latest) const;
    QStringList splitReplyMessages(const QString& text);
    void finishReply();
    void failReply(const QString& reason);
    void previewImage(const QString& path);
    void appendTraceStart(const AiProviderConfig& config, const AiRequest& request);
    void appendTraceFinish(const QString& status, const QStringList& finalMessages, const QString& detail = QString());

    QJsonObject m_robot;
    QString m_robotId;
    QString m_robotName;
    AiChatAppService* m_aiService = nullptr;
    IAiStreamingSession* m_session = nullptr;
    QScrollArea* m_chatScroll = nullptr;
    QWidget* m_chatBody = nullptr;
    QVBoxLayout* m_messageLayout = nullptr;
    QPlainTextEdit* m_inputEdit = nullptr;
    QPushButton* m_clearButton = nullptr;
    QPushButton* m_sendButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_candidateLabel = nullptr;
    QWidget* m_pendingRow = nullptr;
    QLabel* m_pendingLabel = nullptr;
    QString m_accumulated;
    QString m_traceId;
    QString m_lastQuestion;
    QString m_lastKnowledgeQuery;
    QString m_lastKnowledgeStatus;
    QString m_lastImageStatus;
    QString m_lastSplitStatus;
    QString m_lastLinkedImageName;
    QList<KnowledgeSnippetContext> m_lastKnowledgeSnippets;
    QList<ImageCandidate> m_imageCandidates;
    bool m_busy = false;
    bool m_traceFinished = false;
};

#endif // ROBOTSANDBOXDIALOG_H
