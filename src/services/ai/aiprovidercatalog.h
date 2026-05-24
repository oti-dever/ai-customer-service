#ifndef AIPROVIDERCATALOG_H
#define AIPROVIDERCATALOG_H

#include "aitypes.h"

#include <QList>

struct AiPresetDefinition {
    QString sessionModelKey;
    QString label;
    QString defaultBaseUrl;
    QString defaultModel;
    QString apiKeyPlaceholder;
    QString assistantDisplayName;
    QString assistantAvatarResource;
    bool available = false;
    AiProviderCapabilities capabilities;
};

struct AiConfigLoadOptions {
    bool allowAggregateFallback = false;
    bool allowGeneralFallback = false;
};

QList<AiPresetDefinition> aiPresetDefinitions();
AiPresetDefinition aiPresetDefinition(const QString& sessionModelKey);
QString aiPresetSettingsGroup(const QString& sessionModelKey);
QString aiPresetLabel(const QString& sessionModelKey);
AiProviderConfig loadAiProviderConfig(const QString& sessionModelKey,
                                     const AiConfigLoadOptions& options = {});
void saveAiProviderConfig(const AiProviderConfig& config);
void migrateLegacyAiSettingsToPreset(const QString& sessionModelKey);

#endif // AIPROVIDERCATALOG_H
