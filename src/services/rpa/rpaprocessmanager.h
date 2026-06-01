#ifndef RPAPROCESSMANAGER_H
#define RPAPROCESSMANAGER_H

#include "rpatypes.h"
#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QSharedPointer>
#include <QStringDecoder>

namespace Rpa {

class RpaProcessManager : public QObject
{
    Q_OBJECT
public:
    static RpaProcessManager& instance();

    void initialize();
    void shutdown();

    PlatformStatus status(const QString& platformId) const;
    QVector<PlatformStatus> allStatuses() const;
    QStringList runningPlatformIds() const;

    QString processLog(const QString& platformId) const;
    void clearProcessLog(const QString& platformId);

    void startPlatform(const QString& platformId);
    void stopPlatform(const QString& platformId);
    void startPlatforms(const QStringList& platformIds);
    void stopPlatforms(const QStringList& platformIds);
    void stopAll();

signals:
    void platformStateChanged(const QString& platformId, ProcessState newState);
    void logAppended(const QString& platformId, const QString& text);
    void errorOccurred(const QString& platformId, const QString& error);

private:
    explicit RpaProcessManager(QObject* parent = nullptr);
    ~RpaProcessManager();

    void appendProcessLog(const QString& platformId, const QString& text);
    void updateState(const QString& platformId, ProcessState state);
    QString stripAnsiEscapes(const QString& text) const;
    bool isValidPlatformId(const QString& platformId) const;

    struct ProcessInfo {
        QProcess* process = nullptr;
        QSharedPointer<QStringDecoder> decoder;
        ProcessState state = ProcessState::Stopped;
        QDateTime startedAt;
        QString lastError;
    };

    QMap<QString, ProcessInfo> m_processes;
    QMap<QString, QString> m_logs;
    QString m_pythonRoot;
    int m_maxLogChars = 400000;
};

} // namespace Rpa

#endif // RPAPROCESSMANAGER_H
