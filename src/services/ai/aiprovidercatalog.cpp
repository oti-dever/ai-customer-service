#include "aiprovidercatalog.h"

#include "../../utils/appsettings.h"

#include <QSettings>

namespace {

AiPresetDefinition buildDeepSeekDefinition()
{
    AiPresetDefinition def;
    def.sessionModelKey = QStringLiteral("deepseek:deepseek-chat");
    def.label = QStringLiteral("DeepSeek");
    def.defaultBaseUrl = QStringLiteral("https://api.deepseek.com");
    def.defaultModel = QStringLiteral("deepseek-chat");
    def.apiKeyPlaceholder = QStringLiteral("在 DeepSeek 开放平台创建的 API Key");
    def.assistantDisplayName = QStringLiteral("DeepSeek");
    def.assistantAvatarResource
        = QStringLiteral(":/aggregate_reception_icons/deepseek_logo_icon.svg");
    def.available = true;
    def.capabilities.supportsStreamingChat = true;
    return def;
}

AiPresetDefinition buildDoubaoDefinition()
{
    AiPresetDefinition def;
    def.sessionModelKey = QStringLiteral("doubao:ark");
    def.label = QStringLiteral("豆包");
    def.defaultBaseUrl = QStringLiteral("https://ark.cn-beijing.volces.com/api/v3");
    def.apiKeyPlaceholder = QStringLiteral("火山方舟控制台创建的 API Key");
    def.assistantDisplayName = QStringLiteral("doubao");
    def.assistantAvatarResource
        = QStringLiteral(":/aggregate_reception_icons/doubao_logo_icon.svg");
    def.available = true;
    def.capabilities.supportsStreamingChat = true;
    def.capabilities.supportsVisionDataUrl = true;
    def.capabilities.supportsFileAttachment = true;
    def.capabilities.supportsArkResponses = true;
    return def;
}

AiPresetDefinition buildQwenPlaceholderDefinition()
{
    AiPresetDefinition def;
    def.sessionModelKey = QStringLiteral("qwen:placeholder");
    def.label = QStringLiteral("通义千问（即将支持）");
    def.apiKeyPlaceholder = QStringLiteral("在阿里云等平台创建的 API Key（接入后填写）");
    def.assistantDisplayName = QStringLiteral("通义千问");
    def.assistantAvatarResource
        = QStringLiteral(":/aggregate_reception_icons/tongyi_qianwen_logo_icon.svg");
    return def;
}

QList<AiPresetDefinition> allDefinitions()
{
    return {
        buildDeepSeekDefinition(),
        buildQwenPlaceholderDefinition(),
        buildDoubaoDefinition(),
    };
}

} // namespace

QList<AiPresetDefinition> aiPresetDefinitions()
{
    return allDefinitions();
}

AiPresetDefinition aiPresetDefinition(const QString& sessionModelKey)
{
    for (const AiPresetDefinition& def : allDefinitions()) {
        if (def.sessionModelKey == sessionModelKey)
            return def;
    }
    AiPresetDefinition def;
    def.sessionModelKey = sessionModelKey;
    def.label = sessionModelKey;
    return def;
}

QString aiPresetSettingsGroup(const QString& sessionModelKey)
{
    const QString slug = QString(sessionModelKey).replace(QLatin1Char(':'), QLatin1Char('_'));
    return QStringLiteral("ai/presets/%1").arg(slug);
}

QString aiPresetLabel(const QString& sessionModelKey)
{
    return aiPresetDefinition(sessionModelKey).label;
}

AiProviderConfig loadAiProviderConfig(const QString& sessionModelKey, const AiConfigLoadOptions& options)
{
    const AiPresetDefinition def = aiPresetDefinition(sessionModelKey);
    AiProviderConfig config;
    config.sessionModelKey = sessionModelKey;
    config.capabilities = def.capabilities;

    QSettings settings = AppSettings::create();
    settings.beginGroup(aiPresetSettingsGroup(sessionModelKey));
    config.baseUrl = settings.value(QStringLiteral("baseUrl"), def.defaultBaseUrl).toString().trimmed();
    config.model = settings.value(QStringLiteral("model"), def.defaultModel).toString().trimmed();
    config.apiKey = settings.value(QStringLiteral("apiKey")).toString().trimmed();
    settings.endGroup();

    if (options.allowAggregateFallback) {
        if (config.apiKey.isEmpty()) {
            config.apiKey = settings.value(QStringLiteral("aggregateAi/apiKey")).toString().trimmed();
            if (config.apiKey.isEmpty() && options.allowGeneralFallback)
                config.apiKey = settings.value(QStringLiteral("ai/apiKey")).toString().trimmed();
        }
        if (config.baseUrl.isEmpty()) {
            config.baseUrl = settings.value(QStringLiteral("aggregateAi/baseUrl")).toString().trimmed();
            if (config.baseUrl.isEmpty() && options.allowGeneralFallback)
                config.baseUrl = settings.value(QStringLiteral("ai/baseUrl")).toString().trimmed();
        }
        if (config.model.isEmpty()) {
            config.model = settings.value(QStringLiteral("aggregateAi/model")).toString().trimmed();
            if (config.model.isEmpty() && options.allowGeneralFallback)
                config.model = settings.value(QStringLiteral("ai/model")).toString().trimmed();
        }
    } else if (options.allowGeneralFallback) {
        if (config.apiKey.isEmpty())
            config.apiKey = settings.value(QStringLiteral("ai/apiKey")).toString().trimmed();
        if (config.baseUrl.isEmpty())
            config.baseUrl = settings.value(QStringLiteral("ai/baseUrl")).toString().trimmed();
        if (config.model.isEmpty())
            config.model = settings.value(QStringLiteral("ai/model")).toString().trimmed();
    }

    return config;
}

void saveAiProviderConfig(const AiProviderConfig& config)
{
    if (config.sessionModelKey.isEmpty())
        return;
    QSettings settings = AppSettings::create();
    settings.beginGroup(aiPresetSettingsGroup(config.sessionModelKey));
    settings.setValue(QStringLiteral("baseUrl"), config.baseUrl.trimmed());
    settings.setValue(QStringLiteral("model"), config.model.trimmed());
    settings.setValue(QStringLiteral("apiKey"), config.apiKey);
    settings.endGroup();
}

void migrateLegacyAiSettingsToPreset(const QString& sessionModelKey)
{
    QSettings settings = AppSettings::create();
    const QString legacyBase = settings.value(QStringLiteral("ai/baseUrl")).toString().trimmed();
    const QString legacyModel = settings.value(QStringLiteral("ai/model")).toString().trimmed();
    const QString legacyKey = settings.value(QStringLiteral("ai/apiKey")).toString().trimmed();
    if (legacyBase.isEmpty() && legacyModel.isEmpty() && legacyKey.isEmpty())
        return;

    settings.beginGroup(aiPresetSettingsGroup(sessionModelKey));
    if (!settings.contains(QStringLiteral("baseUrl")) && !legacyBase.isEmpty())
        settings.setValue(QStringLiteral("baseUrl"), legacyBase);
    if (!settings.contains(QStringLiteral("model")) && !legacyModel.isEmpty())
        settings.setValue(QStringLiteral("model"), legacyModel);
    if (!settings.contains(QStringLiteral("apiKey")) && !legacyKey.isEmpty())
        settings.setValue(QStringLiteral("apiKey"), legacyKey);
    settings.endGroup();
}
