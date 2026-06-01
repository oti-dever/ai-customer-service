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
    RpaCommand,
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
    QString platformType;
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

struct RpaCommandRequest {
    QString requestId;
    QString commandType;
    QString platformType;
    QString accountId;
    QString taskId;
    QString targetWindow;
    QJsonObject parameters;

    RpaCommandRequest() : requestId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}
};

struct RpaCommandResponse {
    QString requestId;
    ResponseStatus status = ResponseStatus::Success;
    QString errorMessage;
    QJsonObject result;
    QDateTime respondedAt;
};

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

struct RpaEventBatch {
    ResponseStatus status = ResponseStatus::Success;
    QString errorMessage;
    QString cursor;
    QString latestCursor;
    QVector<QJsonObject> events;
    QDateTime respondedAt;
};

QString toString(RequestType type);
QString toString(ResponseStatus status);
RequestType requestTypeFromString(const QString& value);
ResponseStatus responseStatusFromString(const QString& value);

} // namespace Ipc

Q_DECLARE_METATYPE(Ipc::AiSuggestionRequest)
Q_DECLARE_METATYPE(Ipc::AiSuggestionResponse)
Q_DECLARE_METATYPE(Ipc::RpaCommandRequest)
Q_DECLARE_METATYPE(Ipc::RpaCommandResponse)
Q_DECLARE_METATYPE(Ipc::HealthCheckRequest)
Q_DECLARE_METATYPE(Ipc::HealthCheckResponse)
Q_DECLARE_METATYPE(Ipc::RpaEventBatch)

#endif // IPCTYPES_H
