#include "ipctypes.h"

namespace Ipc {

QString toString(RequestType type)
{
    switch (type) {
    case RequestType::AiSuggestion: return QStringLiteral("ai_suggestion");
    case RequestType::AiChat: return QStringLiteral("ai_chat");
    case RequestType::RpaCommand: return QStringLiteral("rpa_command");
    case RequestType::HealthCheck: return QStringLiteral("health_check");
    case RequestType::Unknown:
    default: return QStringLiteral("unknown");
    }
}

QString toString(ResponseStatus status)
{
    switch (status) {
    case ResponseStatus::Success: return QStringLiteral("success");
    case ResponseStatus::Error: return QStringLiteral("error");
    case ResponseStatus::Timeout: return QStringLiteral("timeout");
    case ResponseStatus::Cancelled: return QStringLiteral("cancelled");
    default: return QStringLiteral("unknown");
    }
}

RequestType requestTypeFromString(const QString& value)
{
    if (value == QLatin1String("ai_suggestion")) return RequestType::AiSuggestion;
    if (value == QLatin1String("ai_chat")) return RequestType::AiChat;
    if (value == QLatin1String("rpa_command")) return RequestType::RpaCommand;
    if (value == QLatin1String("health_check")) return RequestType::HealthCheck;
    return RequestType::Unknown;
}

ResponseStatus responseStatusFromString(const QString& value)
{
    if (value == QLatin1String("success")) return ResponseStatus::Success;
    if (value == QLatin1String("error")) return ResponseStatus::Error;
    if (value == QLatin1String("timeout")) return ResponseStatus::Timeout;
    if (value == QLatin1String("cancelled")) return ResponseStatus::Cancelled;
    return ResponseStatus::Error;
}

} // namespace Ipc
