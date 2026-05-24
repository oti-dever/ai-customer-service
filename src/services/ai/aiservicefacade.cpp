#include "aiservicefacade.h"

#include "airequestassembler.h"
#include "aiprovidercatalog.h"
#include "aistreamingsession.h"

#include <QNetworkAccessManager>

AiServiceFacade::AiServiceFacade(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent)
    , m_nam(nam)
{
}

IAiStreamingSession* AiServiceFacade::createSession(const AiProviderConfig& config,
                                                    const AiRequest& request,
                                                    QObject* parent) const
{
    const QString presetLabel = aiPresetLabel(config.sessionModelKey);
    if (!m_nam)
        return new ImmediateFailAiSession(QStringLiteral("网络未初始化"), parent);

    if (aiRequestContainsLocalFile(request)) {
        if (!config.capabilities.supportsFileAttachment || !config.capabilities.supportsArkResponses) {
            return new ImmediateFailAiSession(
                QStringLiteral("当前模型 %1 不支持文件附件。").arg(presetLabel), parent);
        }
        if (!config.isValidForChat()) {
            return new ImmediateFailAiSession(QStringLiteral("模型配置不完整，请检查 Base URL、API Key 与模型名称。"),
                                              parent);
        }
        return new ArkFileSession(m_nam, config, request, parent);
    }

    if (aiRequestContainsImage(request) && !config.capabilities.supportsVisionDataUrl) {
        return new ImmediateFailAiSession(
            QStringLiteral("当前模型 %1 不支持图片输入。").arg(presetLabel), parent);
    }
    if (!config.capabilities.supportsStreamingChat) {
        return new ImmediateFailAiSession(
            QStringLiteral("当前模型 %1 暂不支持聊天能力。").arg(presetLabel), parent);
    }
    if (!config.isValidForChat()) {
        return new ImmediateFailAiSession(QStringLiteral("模型配置不完整，请检查 Base URL、API Key 与模型名称。"),
                                          parent);
    }

    return new OpenAiChatSession(m_nam, config, request, parent);
}
