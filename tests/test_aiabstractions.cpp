#include <QtTest>

#include "services/ai/aiprovidercatalog.h"
#include "services/ai/airequestassembler.h"
#include "services/ai/aiservicefacade.h"
#include "services/ai/aistreamingsession.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QTemporaryDir>

class TestAiAbstractions : public QObject
{
    Q_OBJECT

private slots:
    void presetDefinition_exposesCapabilities();
    void buildChatMessages_supportsMultimodalUserTurn();
    void buildArkFileRequestData_projectsHistoryAndAttachment();
    void serviceFacade_routesRequestsByCapabilities();
};

void TestAiAbstractions::presetDefinition_exposesCapabilities()
{
    const AiPresetDefinition deepseek = aiPresetDefinition(QStringLiteral("deepseek:deepseek-chat"));
    QVERIFY(deepseek.available);
    QVERIFY(deepseek.capabilities.supportsStreamingChat);
    QVERIFY(!deepseek.capabilities.supportsVisionDataUrl);
    QVERIFY(!deepseek.capabilities.supportsFileAttachment);

    const AiPresetDefinition doubao = aiPresetDefinition(QStringLiteral("doubao:ark"));
    QVERIFY(doubao.available);
    QVERIFY(doubao.capabilities.supportsStreamingChat);
    QVERIFY(doubao.capabilities.supportsVisionDataUrl);
    QVERIFY(doubao.capabilities.supportsFileAttachment);
    QVERIFY(doubao.capabilities.supportsArkResponses);
}

void TestAiAbstractions::buildChatMessages_supportsMultimodalUserTurn()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath(QStringLiteral("sample.png"));
    QFile image(imagePath);
    QVERIFY(image.open(QIODevice::WriteOnly));
    static const unsigned char kPngData[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
        0x00, 0x03, 0x01, 0x01, 0x00, 0x18, 0xDD, 0x8D,
        0x18, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
        0x44, 0xAE, 0x42, 0x60, 0x82
    };
    image.write(reinterpret_cast<const char*>(kPngData), sizeof(kPngData));
    image.close();

    AiConversationTurn userTurn;
    userTurn.role = QStringLiteral("user");
    userTurn.parts.append(makeAiImageFilePart(imagePath));
    userTurn.parts.append(makeAiTextPart(QStringLiteral("请结合图片回答")));

    AiRequest request;
    request.systemPrompt = QStringLiteral("系统提示");
    request.turns.append(userTurn);

    QString error;
    const QJsonArray messages = buildChatCompletionsMessages(request, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));

    const QJsonArray content = messages.at(1).toObject().value(QStringLiteral("content")).toArray();
    QCOMPARE(content.size(), 2);
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("image_url"));
    QVERIFY(content.at(0).toObject()
                .value(QStringLiteral("image_url"))
                .toObject()
                .value(QStringLiteral("url"))
                .toString()
                .startsWith(QStringLiteral("data:image/png;base64,")));
    QCOMPARE(content.at(1).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("请结合图片回答"));
}

void TestAiAbstractions::buildArkFileRequestData_projectsHistoryAndAttachment()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("order.pdf"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("pdf");
    file.close();

    AiRequest request;
    request.systemPrompt = QStringLiteral("你是客服助手");
    request.turns.append(makeAiTextTurn(QStringLiteral("user"), QStringLiteral("你好")));
    request.turns.append(makeAiTextTurn(QStringLiteral("assistant"), QStringLiteral("您好，请问需要什么帮助？")));

    AiConversationTurn fileTurn;
    fileTurn.role = QStringLiteral("user");
    fileTurn.parts.append(makeAiLocalFilePart(filePath, QStringLiteral("order.pdf")));
    fileTurn.parts.append(makeAiTextPart(QStringLiteral("请帮我看下附件")));
    request.turns.append(fileTurn);

    AiArkFileRequestData data;
    QString error;
    QVERIFY(buildArkFileRequestData(request, &data, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(data.localFilePath, filePath);
    QCOMPARE(data.userText, QStringLiteral("请帮我看下附件"));
    QCOMPARE(data.instructions, QStringLiteral("你是客服助手"));
    QVERIFY(data.historyPlainText.contains(QStringLiteral("用户: 你好")));
    QVERIFY(data.historyPlainText.contains(QStringLiteral("助手: 您好，请问需要什么帮助？")));
}

void TestAiAbstractions::serviceFacade_routesRequestsByCapabilities()
{
    QNetworkAccessManager nam;
    AiServiceFacade facade(&nam);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("a.pdf"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("pdf");
    file.close();

    AiProviderConfig deepseek = aiPresetDefinition(QStringLiteral("deepseek:deepseek-chat")).available
        ? loadAiProviderConfig(QStringLiteral("deepseek:deepseek-chat"))
        : AiProviderConfig{};
    deepseek.sessionModelKey = QStringLiteral("deepseek:deepseek-chat");
    deepseek.baseUrl = QStringLiteral("https://api.deepseek.com");
    deepseek.apiKey = QStringLiteral("key");
    deepseek.model = QStringLiteral("deepseek-chat");
    deepseek.capabilities = aiPresetDefinition(deepseek.sessionModelKey).capabilities;

    AiRequest textRequest;
    textRequest.turns.append(makeAiTextTurn(QStringLiteral("user"), QStringLiteral("hello")));
    IAiStreamingSession* chatSession = facade.createSession(deepseek, textRequest, this);
    QVERIFY(qobject_cast<OpenAiChatSession*>(chatSession) != nullptr);
    chatSession->deleteLater();

    AiProviderConfig doubao = loadAiProviderConfig(QStringLiteral("doubao:ark"));
    doubao.sessionModelKey = QStringLiteral("doubao:ark");
    doubao.baseUrl = QStringLiteral("https://ark.cn-beijing.volces.com/api/v3");
    doubao.apiKey = QStringLiteral("key");
    doubao.model = QStringLiteral("ep-test");
    doubao.capabilities = aiPresetDefinition(doubao.sessionModelKey).capabilities;

    AiRequest fileRequest;
    AiConversationTurn fileTurn;
    fileTurn.role = QStringLiteral("user");
    fileTurn.parts.append(makeAiLocalFilePart(filePath, QStringLiteral("a.pdf")));
    fileRequest.turns.append(fileTurn);
    IAiStreamingSession* fileSession = facade.createSession(doubao, fileRequest, this);
    QVERIFY(qobject_cast<ArkFileSession*>(fileSession) != nullptr);
    fileSession->deleteLater();

    IAiStreamingSession* rejectedSession = facade.createSession(deepseek, fileRequest, this);
    QVERIFY(qobject_cast<ImmediateFailAiSession*>(rejectedSession) != nullptr);
    rejectedSession->deleteLater();
}

QTEST_MAIN(TestAiAbstractions)
#include "test_aiabstractions.moc"
