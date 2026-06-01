#ifndef RPATYPES_H
#define RPATYPES_H

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace Rpa {

enum class PlatformId {
    Wechat,
    Qianniu,
    Unknown,
};

enum class ProcessState {
    Stopped,
    Starting,
    Running,
    Stopping,
    Error,
};

struct PlatformStatus {
    PlatformId platformId = PlatformId::Unknown;
    ProcessState state = ProcessState::Stopped;
    QString displayName;
    QString lastError;
    QDateTime startedAt;
    QDateTime stoppedAt;
};

QString toString(PlatformId id);
QString toString(ProcessState state);
PlatformId platformIdFromString(const QString& value);
ProcessState processStateFromString(const QString& value);
QStringList allPlatformIds();

} // namespace Rpa

Q_DECLARE_METATYPE(Rpa::PlatformStatus)

#endif // RPATYPES_H
