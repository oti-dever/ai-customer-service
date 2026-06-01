#include <QtTest>

#include "services/ai/openaicompatclient.h"

#include <QNetworkAccessManager>
#include <QSignalSpy>

class TestOpenAiCompatClient : public QObject
{
    Q_OBJECT

private slots:
    void buildCompletionsUrl_normalizesBaseUrl();
    void handleSseLine_emitsDeltaOnlyOnce();
    void processSseBuffer_handlesMultipleLinesAndKeepAlive();
};

void TestOpenAiCompatClient::buildCompletionsUrl_normalizesBaseUrl()
{
    QCOMPARE(OpenAiCompatClient::buildCompletionsUrl(QStringLiteral("https://api.example.com/v1")),
             QStringLiteral("https://api.example.com/v1/chat/completions"));
    QCOMPARE(OpenAiCompatClient::buildCompletionsUrl(QStringLiteral("https://api.example.com/v1/")),
             QStringLiteral("https://api.example.com/v1/chat/completions"));
    QCOMPARE(OpenAiCompatClient::buildCompletionsUrl(QStringLiteral("https://api.example.com/v1/chat/completions")),
             QStringLiteral("https://api.example.com/v1/chat/completions"));
}

void TestOpenAiCompatClient::handleSseLine_emitsDeltaOnlyOnce()
{
    QNetworkAccessManager nam;
    OpenAiCompatClient client(&nam);
    QSignalSpy deltaSpy(&client, &OpenAiCompatClient::streamDelta);

    const QByteArray line =
        "data: {\"choices\":[{\"delta\":{\"content\":\"你好\"},\"message\":{\"content\":\"整段不应重复\"}}]}";

    QVERIFY(client.handleSseLine(line));
    QCOMPARE(deltaSpy.count(), 1);
    QCOMPARE(deltaSpy.takeFirst().at(0).toString(), QStringLiteral("你好"));
}

void TestOpenAiCompatClient::processSseBuffer_handlesMultipleLinesAndKeepAlive()
{
    QNetworkAccessManager nam;
    OpenAiCompatClient client(&nam);
    QSignalSpy deltaSpy(&client, &OpenAiCompatClient::streamDelta);

    client.m_sseBuffer =
        ": keep-alive\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"你\"}}]}\n"
        "\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"好\"}}]}\n"
        "data: [DONE]\n";

    client.processSseBuffer();

    QCOMPARE(deltaSpy.count(), 2);
    QCOMPARE(deltaSpy.at(0).at(0).toString(), QStringLiteral("你"));
    QCOMPARE(deltaSpy.at(1).at(0).toString(), QStringLiteral("好"));
    QVERIFY(client.m_sseBuffer.isEmpty());
}

QTEST_MAIN(TestOpenAiCompatClient)
#include "test_openaicompatclient.moc"
