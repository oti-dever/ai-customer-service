#include "unifiedmodels.h"

namespace {

QString normalized(QString value)
{
    return value.trimmed().toLower();
}

} // namespace

namespace Models {

QString toString(PlatformType value)
{
    switch (value) {
    case PlatformType::Mock: return QStringLiteral("mock");
    case PlatformType::PddWeb: return QStringLiteral("pdd_web");
    case PlatformType::QianniuPc: return QStringLiteral("qianniu");
    case PlatformType::WechatPc: return QStringLiteral("wechat");
    case PlatformType::GenericWeb: return QStringLiteral("generic_web");
    case PlatformType::GenericPc: return QStringLiteral("generic_pc");
    case PlatformType::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString toString(ConversationStatus value)
{
    switch (value) {
    case ConversationStatus::New: return QStringLiteral("new");
    case ConversationStatus::Active: return QStringLiteral("active");
    case ConversationStatus::WaitingCustomer: return QStringLiteral("waiting_customer");
    case ConversationStatus::WaitingAgent: return QStringLiteral("waiting_agent");
    case ConversationStatus::Closed: return QStringLiteral("closed");
    case ConversationStatus::Degraded: return QStringLiteral("degraded");
    case ConversationStatus::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString toString(MessageDirection value)
{
    switch (value) {
    case MessageDirection::Inbound: return QStringLiteral("inbound");
    case MessageDirection::Outbound: return QStringLiteral("outbound");
    case MessageDirection::System: return QStringLiteral("system");
    }
    return QStringLiteral("system");
}

QString toString(MessageContentType value)
{
    switch (value) {
    case MessageContentType::Text: return QStringLiteral("text");
    case MessageContentType::Image: return QStringLiteral("image");
    case MessageContentType::Emoji: return QStringLiteral("emoji");
    case MessageContentType::File: return QStringLiteral("file");
    case MessageContentType::Video: return QStringLiteral("video");
    case MessageContentType::Link: return QStringLiteral("link");
    case MessageContentType::OrderCard: return QStringLiteral("order_card");
    case MessageContentType::ProductCard: return QStringLiteral("product_card");
    case MessageContentType::System: return QStringLiteral("system");
    case MessageContentType::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString toString(MessageStatus value)
{
    switch (value) {
    case MessageStatus::Draft: return QStringLiteral("draft");
    case MessageStatus::Observed: return QStringLiteral("observed");
    case MessageStatus::Pending: return QStringLiteral("pending");
    case MessageStatus::Sent: return QStringLiteral("sent");
    case MessageStatus::Failed: return QStringLiteral("failed");
    case MessageStatus::Unknown: return QStringLiteral("unknown");
    case MessageStatus::Recalled: return QStringLiteral("recalled");
    }
    return QStringLiteral("unknown");
}

QString toString(SourceType value)
{
    switch (value) {
    case SourceType::Mock: return QStringLiteral("mock");
    case SourceType::DomObserved: return QStringLiteral("dom_observed");
    case SourceType::UiObserved: return QStringLiteral("ui_observed");
    case SourceType::OcrExtracted: return QStringLiteral("ocr_extracted");
    case SourceType::NotificationObserved: return QStringLiteral("notification_observed");
    case SourceType::ManualConfirmed: return QStringLiteral("manual_confirmed");
    case SourceType::Experimental: return QStringLiteral("experimental");
    }
    return QStringLiteral("experimental");
}

QString toString(VerificationStatus value)
{
    switch (value) {
    case VerificationStatus::Unverified: return QStringLiteral("unverified");
    case VerificationStatus::AutoVerified: return QStringLiteral("auto_verified");
    case VerificationStatus::ManualVerified: return QStringLiteral("manual_verified");
    case VerificationStatus::Conflict: return QStringLiteral("conflict");
    }
    return QStringLiteral("unverified");
}

QString toString(PlatformAccountStatus value)
{
    switch (value) {
    case PlatformAccountStatus::Offline: return QStringLiteral("offline");
    case PlatformAccountStatus::Starting: return QStringLiteral("starting");
    case PlatformAccountStatus::Online: return QStringLiteral("online");
    case PlatformAccountStatus::Degraded: return QStringLiteral("degraded");
    case PlatformAccountStatus::Error: return QStringLiteral("error");
    case PlatformAccountStatus::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

PlatformType platformTypeFromString(const QString& value)
{
    const QString v = normalized(value);
    if (v == QLatin1String("mock") || v == QLatin1String("simulator")) return PlatformType::Mock;
    if (v == QLatin1String("pdd_web")) return PlatformType::PddWeb;
    if (v == QLatin1String("qianniu")) return PlatformType::QianniuPc;
    if (v == QLatin1String("wechat")) return PlatformType::WechatPc;
    if (v == QLatin1String("generic_web")) return PlatformType::GenericWeb;
    if (v == QLatin1String("generic_pc")) return PlatformType::GenericPc;
    return PlatformType::Unknown;
}

ConversationStatus conversationStatusFromString(const QString& value)
{
    const QString v = normalized(value);
    if (v == QLatin1String("new")) return ConversationStatus::New;
    if (v == QLatin1String("active") || v == QLatin1String("open")) return ConversationStatus::Active;
    if (v == QLatin1String("waiting_customer")) return ConversationStatus::WaitingCustomer;
    if (v == QLatin1String("waiting_agent")) return ConversationStatus::WaitingAgent;
    if (v == QLatin1String("closed")) return ConversationStatus::Closed;
    if (v == QLatin1String("degraded")) return ConversationStatus::Degraded;
    return ConversationStatus::Unknown;
}

MessageDirection messageDirectionFromString(const QString& value)
{
    const QString v = normalized(value);
    if (v == QLatin1String("inbound") || v == QLatin1String("in")) return MessageDirection::Inbound;
    if (v == QLatin1String("outbound") || v == QLatin1String("out")) return MessageDirection::Outbound;
    return MessageDirection::System;
}

MessageContentType messageContentTypeFromString(const QString& value)
{
    const QString v = normalized(value);
    if (v == QLatin1String("text")) return MessageContentType::Text;
    if (v == QLatin1String("image")) return MessageContentType::Image;
    if (v == QLatin1String("emoji")) return MessageContentType::Emoji;
    if (v == QLatin1String("file")) return MessageContentType::File;
    if (v == QLatin1String("video")) return MessageContentType::Video;
    if (v == QLatin1String("link")) return MessageContentType::Link;
    if (v == QLatin1String("order_card")) return MessageContentType::OrderCard;
    if (v == QLatin1String("product_card")) return MessageContentType::ProductCard;
    if (v == QLatin1String("system")) return MessageContentType::System;
    return MessageContentType::Unknown;
}

MessageStatus messageStatusFromString(const QString& value)
{
    const QString v = normalized(value);
    if (v == QLatin1String("draft")) return MessageStatus::Draft;
    if (v == QLatin1String("observed")) return MessageStatus::Observed;
    if (v == QLatin1String("pending")) return MessageStatus::Pending;
    if (v == QLatin1String("sent")) return MessageStatus::Sent;
    if (v == QLatin1String("failed")) return MessageStatus::Failed;
    if (v == QLatin1String("recalled")) return MessageStatus::Recalled;
    return MessageStatus::Unknown;
}

SourceType sourceTypeFromString(const QString& value)
{
    const QString v = normalized(value);
    if (v == QLatin1String("mock") || v == QLatin1String("simulator")) return SourceType::Mock;
    if (v == QLatin1String("dom_observed")) return SourceType::DomObserved;
    if (v == QLatin1String("ui_observed")) return SourceType::UiObserved;
    if (v == QLatin1String("ocr_extracted")) return SourceType::OcrExtracted;
    if (v == QLatin1String("notification_observed")) return SourceType::NotificationObserved;
    if (v == QLatin1String("manual_confirmed")) return SourceType::ManualConfirmed;
    return SourceType::Experimental;
}

VerificationStatus verificationStatusFromString(const QString& value)
{
    const QString v = normalized(value);
    if (v == QLatin1String("auto_verified")) return VerificationStatus::AutoVerified;
    if (v == QLatin1String("manual_verified")) return VerificationStatus::ManualVerified;
    if (v == QLatin1String("conflict")) return VerificationStatus::Conflict;
    return VerificationStatus::Unverified;
}

PlatformAccountStatus platformAccountStatusFromString(const QString& value)
{
    const QString v = normalized(value);
    if (v == QLatin1String("offline")) return PlatformAccountStatus::Offline;
    if (v == QLatin1String("starting")) return PlatformAccountStatus::Starting;
    if (v == QLatin1String("online")) return PlatformAccountStatus::Online;
    if (v == QLatin1String("degraded")) return PlatformAccountStatus::Degraded;
    if (v == QLatin1String("error")) return PlatformAccountStatus::Error;
    return PlatformAccountStatus::Unknown;
}

int defaultConfidence(SourceType sourceType)
{
    switch (sourceType) {
    case SourceType::Mock: return 100;
    case SourceType::DomObserved: return 80;
    case SourceType::UiObserved: return 70;
    case SourceType::OcrExtracted: return 55;
    case SourceType::NotificationObserved: return 45;
    case SourceType::ManualConfirmed: return 95;
    case SourceType::Experimental: return 50;
    }
    return 50;
}

MessageStatus messageStatusFromLegacySyncStatus(int syncStatus)
{
    switch (syncStatus) {
    case 10: return MessageStatus::Pending;
    case 11: return MessageStatus::Sent;
    case 12: return MessageStatus::Failed;
    case 1: return MessageStatus::Observed;
    default: return MessageStatus::Unknown;
    }
}

int legacySyncStatusFromMessageStatus(MessageStatus status)
{
    switch (status) {
    case MessageStatus::Pending: return 10;
    case MessageStatus::Sent: return 11;
    case MessageStatus::Failed: return 12;
    case MessageStatus::Observed: return 1;
    case MessageStatus::Draft:
    case MessageStatus::Unknown:
    case MessageStatus::Recalled:
        return 1;
    }
    return 1;
}

QString legacyDirectionFromMessageDirection(MessageDirection direction)
{
    if (direction == MessageDirection::Outbound)
        return QStringLiteral("out");
    if (direction == MessageDirection::System)
        return QStringLiteral("system");
    return QStringLiteral("in");
}

MessageDirection messageDirectionFromLegacy(const QString& direction, const QString& sender)
{
    const QString d = normalized(direction);
    const QString s = normalized(sender);
    if (d == QLatin1String("out") || d == QLatin1String("outbound") || d == QLatin1String("outgoing")
        || d == QLatin1String("sent") || d == QLatin1String("send") || d == QLatin1String("agent")
        || d == QLatin1String("assistant") || d == QLatin1String("me") || d == QLatin1String("self"))
        return MessageDirection::Outbound;
    if (d == QLatin1String("system") || s == QLatin1String("system"))
        return MessageDirection::System;
    if (d == QLatin1String("in") || d == QLatin1String("inbound") || d == QLatin1String("incoming")
        || d == QLatin1String("received") || d == QLatin1String("receive") || d == QLatin1String("customer")
        || d == QLatin1String("user"))
        return MessageDirection::Inbound;
    if (s == QLatin1String("agent") || s == QLatin1String("assistant") || s == QLatin1String("me")
        || s == QLatin1String("self") || s == QLatin1String("out"))
        return MessageDirection::Outbound;
    if (s == QLatin1String("customer") || s == QLatin1String("user") || s == QLatin1String("in"))
        return MessageDirection::Inbound;
    return MessageDirection::Inbound;
}

} // namespace Models
