#ifndef IPCTYPES_H
#define IPCTYPES_H

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QUuid>
#include <QVector>

namespace Ipc {

enum class RequestType {
    AiSuggestion,
    AiChat,
    PlatformCommand,
    HealthCheck,
    Unknown,
};

enum class ResponseStatus {
    Success,
    Error,
    Timeout,
    Cancelled,
};

struct AiSuggestionRequest {
    QString requestId;
    int conversationId = 0;
    QString platform;
    QVector<QPair<QString, QString>> recentMessages;
    QString customerContext;
    int maxSuggestions = 3;
    QJsonObject metadata;

    AiSuggestionRequest() : requestId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}
};

struct AiSuggestionResponse {
    QString requestId;
    ResponseStatus status = ResponseStatus::Success;
    QString errorMessage;
    QVector<QString> suggestions;
    QVector<int> confidences;
    QJsonObject metadata;
    QDateTime respondedAt;
};

struct PlatformCommandRequest {
    QString requestId;
    QString commandType;
    QString platform;
    QString accountId;
    QString taskId;
    QString targetWindow;
    QJsonObject parameters;

    PlatformCommandRequest() : requestId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}
};

struct PlatformCommandResponse {
    QString requestId;
    ResponseStatus status = ResponseStatus::Success;
    QString errorMessage;
    QJsonObject result;
    QDateTime respondedAt;
};

using RpaCommandRequest = PlatformCommandRequest;
using RpaCommandResponse = PlatformCommandResponse;

struct HealthCheckRequest {
    QString requestId;
    QString serviceName;

    HealthCheckRequest() : requestId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}
};

struct HealthCheckResponse {
    QString requestId;
    ResponseStatus status = ResponseStatus::Success;
    bool healthy = false;
    QString version;
    QString errorMessage;
    QDateTime respondedAt;
};

QString toString(RequestType type);
QString toString(ResponseStatus status);
RequestType requestTypeFromString(const QString& value);
ResponseStatus responseStatusFromString(const QString& value);

} // namespace Ipc

Q_DECLARE_METATYPE(Ipc::AiSuggestionRequest)
Q_DECLARE_METATYPE(Ipc::AiSuggestionResponse)
Q_DECLARE_METATYPE(Ipc::PlatformCommandRequest)
Q_DECLARE_METATYPE(Ipc::PlatformCommandResponse)
Q_DECLARE_METATYPE(Ipc::HealthCheckRequest)
Q_DECLARE_METATYPE(Ipc::HealthCheckResponse)

#endif // IPCTYPES_H
