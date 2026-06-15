#ifndef PYTHONSERVICECONTROLLER_H
#define PYTHONSERVICECONTROLLER_H

#include <QObject>
#include <QProcess>
#include <QStringList>

class QProcess;
class QTimer;

class PythonServiceController : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Stopped,
        Starting,
        Running,
        ExternalRunning,
        Stopping,
        Failed
    };
    Q_ENUM(State)

    static PythonServiceController& instance();

    State state() const { return m_state; }
    bool isManagedServiceRunning() const;
    bool isBusy() const;
    QString stateText() const;
    QStringList humanLogs() const { return m_humanLogs; }

    void startService();
    void stopService();
    void refreshConnectionState();

signals:
    void stateChanged(PythonServiceController::State state);
    void logAppended(const QString& line);

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void pollStartupHealth();

private:
    explicit PythonServiceController(QObject* parent = nullptr);

    void setState(State state);
    void appendHumanLog(const QString& line);
    void appendProcessOutput(const QByteArray& chunk);
    QString translateProcessLine(const QString& line) const;
    bool configuredEndpointIsLocal(QString* hostOut = nullptr, int* portOut = nullptr) const;
    void finishAsConnected(State connectedState, const QString& message);
    void scheduleForceKill();

    QProcess* m_process = nullptr;
    QTimer* m_startupPollTimer = nullptr;
    State m_state = State::Stopped;
    QStringList m_humanLogs;
    int m_startupPollsRemaining = 0;
    bool m_stopRequested = false;
};

#endif // PYTHONSERVICECONTROLLER_H
