#ifndef RPAPROCESSCONTROLLER_H
#define RPAPROCESSCONTROLLER_H

#include <QMap>
#include <QObject>
#include <QProcess>
#include <QSharedPointer>
#include <QStringDecoder>
#include <QStringList>

class RpaProcessController : public QObject
{
    Q_OBJECT
public:
    explicit RpaProcessController(QObject* parent = nullptr);

    QString processLog(const QString& platformId) const;
    void clearProcessLog(const QString& platformId);
    QStringList runningPlatformIds() const;
    void startPlatforms(const QStringList& platformIds);
    void stopPlatforms(const QStringList& platformIds);

signals:
    void logAppended(const QString& platformId, const QString& text);
    void statusMessageRequested(const QString& text, int timeoutMs);

private:
    void appendProcessLog(const QString& platformId, const QString& text);

    QMap<QString, QProcess*> m_processes;
    QMap<QString, QSharedPointer<QStringDecoder>> m_consoleDecoders;
    QMap<QString, QString> m_processLogs;
};

#endif // RPAPROCESSCONTROLLER_H
