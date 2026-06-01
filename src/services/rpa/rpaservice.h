#ifndef RPASERVICE_H
#define RPASERVICE_H

#include "rpatypes.h"
#include <QObject>

namespace Rpa {

class RpaProcessManager;

class RpaService : public QObject
{
    Q_OBJECT
public:
    static RpaService& instance();

    void initialize();
    void shutdown();

    PlatformStatus platformStatus(const QString& platformId) const;
    QVector<PlatformStatus> allPlatformStatuses() const;
    QStringList runningPlatforms() const;

    QString platformLog(const QString& platformId) const;
    void clearPlatformLog(const QString& platformId);

public slots:
    void startPlatform(const QString& platformId);
    void stopPlatform(const QString& platformId);
    void startAllPlatforms();
    void stopAllPlatforms();

signals:
    void platformStateChanged(const QString& platformId, ProcessState newState);
    void platformLogAppended(const QString& platformId, const QString& text);
    void platformError(const QString& platformId, const QString& error);
    void statusMessage(const QString& message, int timeoutMs);

private:
    explicit RpaService(QObject* parent = nullptr);

    RpaProcessManager* m_manager = nullptr;
};

} // namespace Rpa

#endif // RPASERVICE_H
