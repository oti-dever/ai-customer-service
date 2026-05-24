#ifndef OPENAICOMPATCLIENT_H
#define OPENAICOMPATCLIENT_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * Minimal OpenAI-compatible chat completions client (DeepSeek, etc.).
 * Stream mode: parses SSE including lines starting with ":" (keep-alive).
 */
class OpenAiCompatClient : public QObject
{
    Q_OBJECT
    friend class TestOpenAiCompatClient;
public:
    explicit OpenAiCompatClient(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~OpenAiCompatClient() override;

    void abortActive();

    /**
     * POST JSON to completions URL; emits streamDelta / completed / failed on main thread.
     * 对火山方舟域名会自动附加 thinking=disabled（关闭豆包 Seed 等模型的思考链，除非 extraRootFields 覆盖）。
     * extraRootFields 会与根对象合并（后者覆盖同名键），可用于 max_tokens 等。
     */
    void requestChatCompletion(const QString& completionsUrl,
                               const QString& apiKey,
                               const QString& model,
                               const QJsonArray& messages,
                               bool stream,
                               const QJsonObject& extraRootFields = {});

    static QString buildCompletionsUrl(const QString& baseUrl);

signals:
    void streamDelta(const QString& delta);
    void completed();
    void failed(const QString& reason);

private slots:
    void onReadyRead();
    void onReplyFinished();

private:
    void processSseBuffer();
    bool handleSseLine(const QByteArray& line);

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
    QByteArray m_sseBuffer;
    bool m_streamMode = false;
};

#endif
