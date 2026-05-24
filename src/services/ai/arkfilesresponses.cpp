#include "arkfilesresponses.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QMimeDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

namespace {

QString extractApiErrorMessage(const QByteArray& body)
{
    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(body, &err);
    if (!jd.isObject())
        return {};
    const QJsonObject o = jd.object();
    const QJsonObject e = o.value(QStringLiteral("error")).toObject();
    QString m = e.value(QStringLiteral("message")).toString();
    if (!m.isEmpty())
        return m;
    m = o.value(QStringLiteral("message")).toString();
    if (!m.isEmpty())
        return m;
    m = e.value(QStringLiteral("code")).toString();
    if (!e.isEmpty() && !m.isEmpty())
        return m;
    return {};
}

/** multipart 里 filename 含中文等时，部分网关会返回 400；用安全 ASCII 名保留扩展名。 */
static QString safeMultipartFilename(const QString& localPath)
{
    const QFileInfo fi(localPath);
    const QString ext = fi.suffix();
    bool ascii = true;
    const QString base = fi.fileName();
    for (QChar c : base) {
        if (c.unicode() > 127) {
            ascii = false;
            break;
        }
    }
    if (ascii)
        return base;
    return ext.isEmpty() ? QStringLiteral("upload.bin")
                         : QStringLiteral("upload.%1").arg(ext);
}

} // namespace

VolcengineArkFileChatService::VolcengineArkFileChatService(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent)
    , m_nam(nam)
{
}

VolcengineArkFileChatService::~VolcengineArkFileChatService()
{
    abort();
}

void VolcengineArkFileChatService::abort()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_sseBuffer.clear();
    m_fileId.clear();
    m_pollAttempts = 0;
}

bool VolcengineArkFileChatService::isLocalFileSupportedByArkFilesApi(const QString& absolutePath)
{
    QMimeDatabase db;
    const QMimeType mt = db.mimeTypeForFile(absolutePath);
    const QString n = mt.name();
    if (n.startsWith(QStringLiteral("text/")))
        return false;
    if (n == QStringLiteral("application/pdf"))
        return true;
    if (n.startsWith(QStringLiteral("image/")))
        return true;
    if (n.startsWith(QStringLiteral("video/")))
        return true;
    // 其它类型（如未知扩展名被标成 application/octet-stream）仍尝试上传，由服务端判定
    return true;
}

QString VolcengineArkFileChatService::normalizeApiBase(const QString& apiBaseUrl)
{
    QString s = apiBaseUrl.trimmed();
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    const QString bad = QStringLiteral("/chat/completions");
    if (s.endsWith(bad, Qt::CaseInsensitive))
        s.chop(bad.size());
    return s;
}

void VolcengineArkFileChatService::fail(const QString& reason)
{
    abort();
    emit failed(reason);
}

void VolcengineArkFileChatService::start(const QString& apiBaseUrl,
                                         const QString& apiKey,
                                         const QString& model,
                                         const QString& localFilePath,
                                         const QString& userText,
                                         const QString& instructions,
                                         const QString& historyPlainText)
{
    abort();

    m_apiBase = normalizeApiBase(apiBaseUrl);
    m_apiKey = apiKey.trimmed();
    m_model = model.trimmed();
    m_localPath = localFilePath;
    m_userText = userText;
    m_instructions = instructions;
    m_historyPlain = historyPlainText;

    if (!m_nam) {
        fail(QStringLiteral("网络未初始化"));
        return;
    }
    if (m_apiBase.isEmpty() || m_apiKey.isEmpty() || m_model.isEmpty()) {
        fail(QStringLiteral("API 配置不完整"));
        return;
    }
    QFileInfo fi(m_localPath);
    if (!fi.exists() || !fi.isReadable()) {
        fail(QStringLiteral("无法读取本地文件"));
        return;
    }
    if (!isLocalFileSupportedByArkFilesApi(m_localPath)) {
        QMimeDatabase db;
        const QString mime = db.mimeTypeForFile(m_localPath).name();
        fail(QStringLiteral(
            "方舟 Files API 不支持该文件类型（%1）。纯文本请粘贴到输入框直接发送，或导出为 PDF 后再用「添加文件」。")
                 .arg(mime));
        return;
    }

    startUpload();
}

void VolcengineArkFileChatService::startUpload()
{
    QUrl u(m_apiBase + QStringLiteral("/files"));
    if (!u.isValid()) {
        fail(QStringLiteral("无效的 API Base URL"));
        return;
    }

    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart purposePart;
    purposePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QVariant(QStringLiteral("form-data; name=\"purpose\"")));
    purposePart.setBody(QByteArrayLiteral("user_data"));
    multi->append(purposePart);

    QFile* file = new QFile(m_localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete multi;
        fail(QStringLiteral("无法打开文件"));
        return;
    }
    const QString fname = safeMultipartFilename(m_localPath);
    QMimeDatabase mimeDb;
    const QMimeType mt = mimeDb.mimeTypeForFile(m_localPath);
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(fname)));
    if (mt.isValid() && !mt.isDefault())
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, mt.name());
    else
        filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                           QVariant(QStringLiteral("application/octet-stream")));
    filePart.setBodyDevice(file);
    file->setParent(multi);
    multi->append(filePart);

    QNetworkRequest req(u);
    req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());

    m_reply = m_nam->post(req, multi);
    multi->setParent(m_reply);

    if (!m_reply) {
        fail(QStringLiteral("无法发起上传"));
        return;
    }
    connect(m_reply, &QNetworkReply::finished, this, &VolcengineArkFileChatService::onUploadFinished);
}

void VolcengineArkFileChatService::onUploadFinished()
{
    if (!m_reply)
        return;
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;

    const QByteArray body = reply->readAll();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError && reply->error() != QNetworkReply::OperationCanceledError) {
        QString r = reply->errorString();
        if (code > 0)
            r = QStringLiteral("HTTP %1 %2").arg(code).arg(r);
        const QString apiMsg = extractApiErrorMessage(body);
        if (!apiMsg.isEmpty())
            r = QStringLiteral("%1（%2）").arg(apiMsg, r);
        fail(r);
        return;
    }
    if (code < 200 || code >= 300) {
        const QString apiMsg = extractApiErrorMessage(body);
        fail(apiMsg.isEmpty() ? QStringLiteral("上传失败 HTTP %1").arg(code) : apiMsg);
        return;
    }

    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !jd.isObject()) {
        fail(QStringLiteral("上传响应解析失败"));
        return;
    }
    const QJsonObject o = jd.object();
    m_fileId = o.value(QStringLiteral("id")).toString().trimmed();
    if (m_fileId.isEmpty()) {
        fail(QStringLiteral("上传未返回 file id"));
        return;
    }

    const QString st = o.value(QStringLiteral("status")).toString();
    if (st == QLatin1String("active")) {
        startResponsesStream();
        return;
    }

    m_pollAttempts = 0;
    schedulePoll();
}

void VolcengineArkFileChatService::schedulePoll()
{
    if (m_fileId.isEmpty()) {
        fail(QStringLiteral("内部错误：无 file id"));
        return;
    }
    QUrl u(m_apiBase + QStringLiteral("/files/") + m_fileId);
    QNetworkRequest req(u);
    req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());

    m_reply = m_nam->get(req);
    if (!m_reply) {
        fail(QStringLiteral("无法查询文件状态"));
        return;
    }
    connect(m_reply, &QNetworkReply::finished, this, &VolcengineArkFileChatService::onPollFinished);
}

void VolcengineArkFileChatService::onPollFinished()
{
    if (!m_reply)
        return;
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;

    const QByteArray body = reply->readAll();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError && reply->error() != QNetworkReply::OperationCanceledError) {
        fail(reply->errorString());
        return;
    }
    if (code < 200 || code >= 300) {
        fail(extractApiErrorMessage(body).isEmpty()
                 ? QStringLiteral("查询文件失败 HTTP %1").arg(code)
                 : extractApiErrorMessage(body));
        return;
    }

    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !jd.isObject()) {
        fail(QStringLiteral("文件状态解析失败"));
        return;
    }
    const QString st = jd.object().value(QStringLiteral("status")).toString();
    if (st == QLatin1String("active")) {
        startResponsesStream();
        return;
    }
    if (st == QLatin1String("failed") || st == QLatin1String("error")) {
        fail(QStringLiteral("文件预处理失败（status=%1）").arg(st));
        return;
    }

    ++m_pollAttempts;
    constexpr int kMaxPoll = 180;
    if (m_pollAttempts >= kMaxPoll) {
        fail(QStringLiteral("等待文件就绪超时，请稍后重试或换较小文件"));
        return;
    }
    QTimer::singleShot(1000, this, &VolcengineArkFileChatService::schedulePoll);
}

void VolcengineArkFileChatService::startResponsesStream()
{
    QUrl u(m_apiBase + QStringLiteral("/responses"));
    if (!u.isValid()) {
        fail(QStringLiteral("无效的 Responses URL"));
        return;
    }

    QJsonArray content;
    content.append(
        QJsonObject{{QStringLiteral("type"), QStringLiteral("input_file")}, {QStringLiteral("file_id"), m_fileId}});

    QString prefix;
    if (!m_historyPlain.trimmed().isEmpty())
        prefix = QStringLiteral("【此前对话】\n%1\n\n").arg(m_historyPlain.trimmed());
    QString userBlock = prefix + m_userText.trimmed();
    if (userBlock.trimmed().isEmpty())
        userBlock = QStringLiteral("请根据附件内容回答问题或做简要摘要。");
    content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")},
                               {QStringLiteral("text"), userBlock}});

    QJsonArray input;
    input.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                             {QStringLiteral("content"), content}});

    QJsonObject root;
    root.insert(QStringLiteral("model"), m_model);
    root.insert(QStringLiteral("input"), input);
    root.insert(QStringLiteral("stream"), true);
    if (!m_instructions.isEmpty())
        root.insert(QStringLiteral("instructions"), m_instructions);

    QJsonObject thinking;
    thinking.insert(QStringLiteral("type"), QStringLiteral("disabled"));
    root.insert(QStringLiteral("thinking"), thinking);

    const QJsonDocument doc(root);
    const QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest req(u);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());

    m_reply = m_nam->post(req, payload);
    if (!m_reply) {
        fail(QStringLiteral("无法发起 Responses 请求"));
        return;
    }
    m_sseBuffer.clear();
    connect(m_reply, &QNetworkReply::readyRead, this, &VolcengineArkFileChatService::onResponsesReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &VolcengineArkFileChatService::onResponsesFinished);
}

void VolcengineArkFileChatService::onResponsesReadyRead()
{
    if (!m_reply)
        return;
    constexpr qsizetype kMaxSseBuffer = 64 * 1024 * 1024;
    const QByteArray chunk = m_reply->readAll();
    if (m_sseBuffer.size() + chunk.size() > kMaxSseBuffer) {
        m_sseBuffer.clear();
        fail(QStringLiteral("流式数据缓冲过大，已中止以防内存耗尽（请缩短输出或联系服务商）。"));
        return;
    }
    m_sseBuffer += chunk;
    processResponsesSseBuffer();
}

void VolcengineArkFileChatService::processResponsesSseBuffer()
{
    for (;;) {
        const int nl = m_sseBuffer.indexOf('\n');
        if (nl < 0)
            break;
        QByteArray line = m_sseBuffer.left(nl);
        m_sseBuffer.remove(0, nl + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (!handleResponsesSseLine(line))
            return;
    }
}

bool VolcengineArkFileChatService::handleResponsesSseLine(const QByteArray& line)
{
    QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return true;
    if (trimmed.startsWith(':'))
        return true;
    if (trimmed.startsWith("event:"))
        return true;

    QByteArray payload;
    if (trimmed.startsWith("data:")) {
        payload = trimmed.mid(5).trimmed();
    } else if (trimmed.startsWith('{')) {
        payload = trimmed;
    } else {
        return true;
    }

    if (payload == "[DONE]")
        return true;

    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !jd.isObject())
        return true;

    const QJsonObject o = jd.object();
    const QString type = o.value(QStringLiteral("type")).toString();

    // 方舟 Responses 流式：增量在 *.delta；*.done / completed 常带全文，若再当增量拼会整段重复。
    if (type.contains(QStringLiteral(".done"), Qt::CaseInsensitive)
        || type.contains(QStringLiteral("completed"), Qt::CaseInsensitive)
        || type.contains(QStringLiteral("response.failed"), Qt::CaseInsensitive)) {
        return true;
    }

    QString delta = o.value(QStringLiteral("delta")).toString();
    if (!delta.isEmpty()) {
        emit textDelta(delta);
        return true;
    }

    const QJsonObject deltaObj = o.value(QStringLiteral("delta")).toObject();
    if (!deltaObj.isEmpty()) {
        const QString t = deltaObj.value(QStringLiteral("text")).toString();
        if (!t.isEmpty()) {
            emit textDelta(t);
            return true;
        }
    }

    const QString text = o.value(QStringLiteral("text")).toString();
    if (!text.isEmpty() && type.contains(QStringLiteral("output_text.delta"), Qt::CaseInsensitive)) {
        emit textDelta(text);
        return true;
    }

    return true;
}

void VolcengineArkFileChatService::onResponsesFinished()
{
    if (!m_reply)
        return;
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;

    const QByteArray tail = reply->readAll();
    if (!tail.isEmpty())
        m_sseBuffer += tail;
    processResponsesSseBuffer();

    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto done = [reply]() { reply->deleteLater(); };

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            done();
            return;
        }
        QString reason = reply->errorString();
        if (code > 0)
            reason = QStringLiteral("HTTP %1 %2").arg(code).arg(reason);
        const QString apiMsg = extractApiErrorMessage(tail.isEmpty() ? m_sseBuffer : tail);
        if (!apiMsg.isEmpty())
            reason = apiMsg;
        emit failed(reason);
        done();
        return;
    }

    if (code < 200 || code >= 300) {
        QString reason = extractApiErrorMessage(tail.isEmpty() ? m_sseBuffer : tail);
        if (reason.isEmpty())
            reason = QStringLiteral("HTTP %1").arg(code > 0 ? code : 0);
        emit failed(reason);
        done();
        return;
    }

    emit completed();
    done();
}
