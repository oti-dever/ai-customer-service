#ifndef OPENAICOMPATCLIENT_H
#define OPENAICOMPATCLIENT_H

#include <QByteArray>
#include <QJsonArray>
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
public:
    explicit OpenAiCompatClient(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~OpenAiCompatClient() override;

    void abortActive();

    /** POST JSON to completions URL; emits streamDelta / completed / failed on main thread. */
    void requestChatCompletion(const QString& completionsUrl,
                               const QString& apiKey,
                               const QString& model,
                               const QJsonArray& messages,
                               bool stream);

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
