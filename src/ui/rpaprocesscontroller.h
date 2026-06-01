#ifndef RPAPROCESSCONTROLLER_H
#define RPAPROCESSCONTROLLER_H

#include <QObject>
#include <QStringList>

namespace Rpa {
class RpaService;
}

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
    Rpa::RpaService* m_service = nullptr;
};

#endif // RPAPROCESSCONTROLLER_H
