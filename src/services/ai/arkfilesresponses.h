#ifndef ARKFILESRESPONSES_H
#define ARKFILESRESPONSES_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * 火山方舟：Files API 上传 → 轮询至 active → Responses API 流式推理。
 * 用于内置 AI 助手「添加文件」验证；与 Chat Completions（OpenAiCompatClient）独立。
 */
class VolcengineArkFileChatService : public QObject
{
    Q_OBJECT
public:
    explicit VolcengineArkFileChatService(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~VolcengineArkFileChatService() override;

    void abort();

    /**
     * @param apiBaseUrl 如 https://ark.cn-beijing.volces.com/api/v3（无尾斜杠）
     * @param instructions 等价 system，写入 Responses 的 instructions 字段（若服务端拒绝则调用方需降级）
     * @param historyPlainText 可选，将上文对话压成纯文本注入本轮 user，避免多轮格式未对齐时丢失上下文
     */
    void start(const QString& apiBaseUrl,
               const QString& apiKey,
               const QString& model,
               const QString& localFilePath,
               const QString& userText,
               const QString& instructions,
               const QString& historyPlainText);

    /**
     * 方舟 Files API 仅支持文档/媒体等类型（如 PDF、图片、视频），**不支持** `text/*` 等纯文本。
     * 纯文本应走 Chat Completions，或先转为 PDF。
     */
    static bool isLocalFileSupportedByArkFilesApi(const QString& absolutePath);

signals:
    void textDelta(const QString& chunk);
    void completed();
    void failed(const QString& reason);

private slots:
    void onUploadFinished();
    void onPollFinished();
    void onResponsesReadyRead();
    void onResponsesFinished();

private:
    void fail(const QString& reason);
    void startUpload();
    void schedulePoll();
    void startResponsesStream();
    void processResponsesSseBuffer();
    bool handleResponsesSseLine(const QByteArray& line);
    static QString normalizeApiBase(const QString& apiBaseUrl);

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
    QByteArray m_sseBuffer;

    QString m_apiBase;
    QString m_apiKey;
    QString m_model;
    QString m_localPath;
    QString m_userText;
    QString m_instructions;
    QString m_historyPlain;

    QString m_fileId;
    int m_pollAttempts = 0;
};

#endif
