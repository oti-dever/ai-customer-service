#ifndef AIREQUESTASSEMBLER_H
#define AIREQUESTASSEMBLER_H

#include "aitypes.h"

#include <QJsonArray>

QJsonArray buildChatCompletionsMessages(const AiRequest& request, QString* errorOut);
bool buildArkFileRequestData(const AiRequest& request, AiArkFileRequestData* out, QString* errorOut);
QString plainTextForAiTurn(const AiConversationTurn& turn);
QString plainTextForAiTurnWithoutFileMarker(const AiConversationTurn& turn);
bool aiRequestContainsImage(const AiRequest& request);
bool aiRequestContainsLocalFile(const AiRequest& request);

#endif // AIREQUESTASSEMBLER_H
