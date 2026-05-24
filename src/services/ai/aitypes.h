#ifndef AITYPES_H
#define AITYPES_H

#include <QJsonObject>
#include <QList>
#include <QString>

enum class AiMessagePartKind {
    Text,
    ImageFile,
    LocalFile,
};

struct AiProviderCapabilities {
    bool supportsStreamingChat = false;
    bool supportsVisionDataUrl = false;
    bool supportsFileAttachment = false;
    bool supportsArkResponses = false;
};

struct AiProviderConfig {
    QString sessionModelKey;
    QString baseUrl;
    QString apiKey;
    QString model;
    AiProviderCapabilities capabilities;

    bool isValidForChat() const
    {
        return !baseUrl.trimmed().isEmpty()
            && !apiKey.trimmed().isEmpty()
            && !model.trimmed().isEmpty();
    }
};

struct AiMessagePart {
    AiMessagePartKind kind = AiMessagePartKind::Text;
    QString text;
    QString filePath;
    QString displayName;
};

struct AiConversationTurn {
    QString role;
    QList<AiMessagePart> parts;
};

struct AiRequest {
    QString systemPrompt;
    QList<AiConversationTurn> turns;
    QJsonObject extraRootFields;
    bool stream = true;
};

struct AiArkFileRequestData {
    QString localFilePath;
    QString userText;
    QString instructions;
    QString historyPlainText;
};

inline AiMessagePart makeAiTextPart(const QString& text)
{
    AiMessagePart part;
    part.kind = AiMessagePartKind::Text;
    part.text = text;
    return part;
}

inline AiMessagePart makeAiImageFilePart(const QString& absolutePath)
{
    AiMessagePart part;
    part.kind = AiMessagePartKind::ImageFile;
    part.filePath = absolutePath;
    return part;
}

inline AiMessagePart makeAiLocalFilePart(const QString& absolutePath, const QString& displayName = {})
{
    AiMessagePart part;
    part.kind = AiMessagePartKind::LocalFile;
    part.filePath = absolutePath;
    part.displayName = displayName;
    return part;
}

inline AiConversationTurn makeAiTextTurn(const QString& role, const QString& text)
{
    AiConversationTurn turn;
    turn.role = role;
    turn.parts.append(makeAiTextPart(text));
    return turn;
}

#endif // AITYPES_H
