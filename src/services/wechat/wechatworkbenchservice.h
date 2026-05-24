#ifndef WECHATWORKBENCHSERVICE_H
#define WECHATWORKBENCHSERVICE_H

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

class QNetworkAccessManager;
class AiServiceFacade;
class IAiStreamingSession;

class WeChatWorkbenchService : public QObject
{
    Q_OBJECT
public:
    explicit WeChatWorkbenchService(QObject* parent = nullptr);
    ~WeChatWorkbenchService() override;

    int probeStatus();
    int listSessions();
    int switchSession(const QString& sessionName);
    int readCurrentMessages(const QString& sessionName = {});
    int sendText(const QString& sessionName, const QString& text);
    void stopProcess();

    bool isPythonServiceActive() const;
    bool startPythonService(QString* errorOut = nullptr);

    void requestAiSuggestion(const QString& sessionName,
                             const QJsonArray& messages,
                             const QString& sessionModelKey);
    void abortAiSuggestion();

signals:
    void pythonServiceActiveChanged(bool active);
    void processLogAppended(const QString& text);
    void commandSucceeded(int requestId, const QString& cmd, const QJsonObject& data);
    void commandFailed(int requestId, const QString& cmd, const QString& reason);
    void aiSuggestionStarted();
    void aiSuggestionDelta(const QString& delta);
    void aiSuggestionCompleted(const QString& text);
    void aiSuggestionFailed(const QString& reason);

private slots:
    void onProcessStdoutReady();
    void onProcessStderrReady();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onAiClientDelta(const QString& delta);
    void onAiClientCompleted();
    void onAiClientFailed(const QString& reason);

private:
    void notifyPythonServiceActiveChanged();
    bool ensurePythonServiceRunning(QString* error = nullptr);
    bool spawnPythonProcess(QString* error = nullptr);
    int sendCommand(const QString& cmd, const QJsonObject& args = {});
    void processStdoutBuffer();
    void processStderrBuffer();
    void handleProtocolLine(const QByteArray& line);
    void clearAiSession();
    QString buildAiSystemPrompt() const;
    QString buildTranscript(const QString& sessionName, const QJsonArray& messages) const;

    QProcess* m_process = nullptr;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    int m_nextRequestId = 1;
    QHash<int, QString> m_pendingCommands;

    QNetworkAccessManager* m_aiNam = nullptr;
    AiServiceFacade* m_aiService = nullptr;
    IAiStreamingSession* m_aiSession = nullptr;
    QString m_aiAccumulated;
    bool m_aiBusy = false;
};

#endif // WECHATWORKBENCHSERVICE_H
