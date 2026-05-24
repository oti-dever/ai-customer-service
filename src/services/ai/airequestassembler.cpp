#include "airequestassembler.h"

#include "../../utils/imagedataurl.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QStringList>

namespace {

QString displayNameForPart(const AiMessagePart& part)
{
    if (!part.displayName.trimmed().isEmpty())
        return part.displayName.trimmed();
    return QFileInfo(part.filePath).fileName();
}

QString plainTextForPart(const AiMessagePart& part, bool includeFileMarker)
{
    switch (part.kind) {
    case AiMessagePartKind::Text:
        return part.text.trimmed();
    case AiMessagePartKind::ImageFile:
        return part.text.trimmed().isEmpty()
            ? QStringLiteral("[图片]")
            : QStringLiteral("[图片] %1").arg(part.text.trimmed());
    case AiMessagePartKind::LocalFile: {
        if (!includeFileMarker)
            return {};
        const QString name = displayNameForPart(part);
        return name.isEmpty()
            ? QStringLiteral("[附件]")
            : QStringLiteral("[附件 %1]").arg(name);
    }
    }
    return {};
}

QJsonValue chatContentForTurn(const AiConversationTurn& turn, QString* errorOut)
{
    const bool hasImage = std::any_of(turn.parts.cbegin(), turn.parts.cend(), [](const AiMessagePart& part) {
        return part.kind == AiMessagePartKind::ImageFile;
    });

    if (!hasImage || turn.role != QLatin1String("user"))
        return plainTextForAiTurn(turn);

    QJsonArray parts;
    for (const AiMessagePart& part : turn.parts) {
        if (part.kind == AiMessagePartKind::Text) {
            const QString text = part.text.trimmed();
            if (!text.isEmpty()) {
                parts.append(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("text"), text},
                });
            }
            continue;
        }
        if (part.kind == AiMessagePartKind::ImageFile) {
            QString dataUrl;
            QString err;
            if (!imageFileToDataUrl(part.filePath, &dataUrl, &err)) {
                if (errorOut)
                    *errorOut = err;
                parts.append(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("text"), QStringLiteral("（图片无法再次加载：%1）").arg(err)},
                });
                continue;
            }
            parts.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("image_url")},
                {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), dataUrl}}},
            });
            continue;
        }

        const QString marker = plainTextForPart(part, true);
        if (!marker.isEmpty()) {
            parts.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("text")},
                {QStringLiteral("text"), marker},
            });
        }
    }

    if (parts.isEmpty())
        return plainTextForAiTurn(turn);
    return parts;
}

int lastUserTurnWithFile(const QList<AiConversationTurn>& turns)
{
    for (int i = turns.size() - 1; i >= 0; --i) {
        if (turns[i].role != QLatin1String("user"))
            continue;
        for (const AiMessagePart& part : turns[i].parts) {
            if (part.kind == AiMessagePartKind::LocalFile)
                return i;
        }
    }
    return -1;
}

QString historySpeakerLabel(const QString& role)
{
    if (role == QLatin1String("assistant"))
        return QStringLiteral("助手");
    if (role == QLatin1String("system"))
        return QStringLiteral("系统");
    return QStringLiteral("用户");
}

} // namespace

QString plainTextForAiTurn(const AiConversationTurn& turn)
{
    QStringList pieces;
    for (const AiMessagePart& part : turn.parts) {
        const QString text = plainTextForPart(part, true);
        if (!text.isEmpty())
            pieces.append(text);
    }
    return pieces.join(QStringLiteral(" ")).trimmed();
}

QString plainTextForAiTurnWithoutFileMarker(const AiConversationTurn& turn)
{
    QStringList pieces;
    for (const AiMessagePart& part : turn.parts) {
        const QString text = plainTextForPart(part, false);
        if (!text.isEmpty())
            pieces.append(text);
    }
    return pieces.join(QStringLiteral(" ")).trimmed();
}

QJsonArray buildChatCompletionsMessages(const AiRequest& request, QString* errorOut)
{
    if (errorOut)
        errorOut->clear();

    QJsonArray arr;
    if (!request.systemPrompt.trimmed().isEmpty()) {
        arr.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("system")},
            {QStringLiteral("content"), request.systemPrompt},
        });
    }
    for (const AiConversationTurn& turn : request.turns) {
        arr.append(QJsonObject{
            {QStringLiteral("role"), turn.role},
            {QStringLiteral("content"), chatContentForTurn(turn, errorOut)},
        });
    }
    return arr;
}

bool buildArkFileRequestData(const AiRequest& request, AiArkFileRequestData* out, QString* errorOut)
{
    if (!out)
        return false;
    if (errorOut)
        errorOut->clear();

    const int index = lastUserTurnWithFile(request.turns);
    if (index < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("未找到可上传的本地文件");
        return false;
    }

    const AiConversationTurn& fileTurn = request.turns.at(index);
    for (const AiMessagePart& part : fileTurn.parts) {
        if (part.kind == AiMessagePartKind::LocalFile) {
            out->localFilePath = QFileInfo(part.filePath).absoluteFilePath();
            break;
        }
    }
    out->userText = plainTextForAiTurnWithoutFileMarker(fileTurn);
    out->instructions = request.systemPrompt;

    QStringList history;
    for (int i = 0; i < index; ++i) {
        const QString text = plainTextForAiTurn(request.turns.at(i));
        if (text.isEmpty())
            continue;
        history.append(QStringLiteral("%1: %2").arg(historySpeakerLabel(request.turns.at(i).role), text));
    }
    out->historyPlainText = history.join(QLatin1Char('\n')).trimmed();
    return !out->localFilePath.isEmpty();
}

bool aiRequestContainsImage(const AiRequest& request)
{
    for (const AiConversationTurn& turn : request.turns) {
        for (const AiMessagePart& part : turn.parts) {
            if (part.kind == AiMessagePartKind::ImageFile)
                return true;
        }
    }
    return false;
}

bool aiRequestContainsLocalFile(const AiRequest& request)
{
    for (const AiConversationTurn& turn : request.turns) {
        for (const AiMessagePart& part : turn.parts) {
            if (part.kind == AiMessagePartKind::LocalFile)
                return true;
        }
    }
    return false;
}
