#include "openaicompatclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

bool jsonLineContent(const QByteArray& line, QByteArray* outPayload)
{
    const QByteArray prefix = "data:";
    QByteArray t = line.trimmed();
    if (!t.startsWith(prefix))
        return false;
    t = t.mid(int(prefix.size())).trimmed();
    *outPayload = t;
    return true;
}

QString extractErrorMessageFromJson(const QByteArray& body)
{
    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(body, &err);
    if (!jd.isObject())
        return {};
    const QJsonObject o = jd.object();
    const QJsonObject e = o.value(QStringLiteral("error")).toObject();
    return e.value(QStringLiteral("message")).toString();
}

} // namespace

OpenAiCompatClient::OpenAiCompatClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent)
    , m_nam(nam)
{
}

OpenAiCompatClient::~OpenAiCompatClient()
{
    abortActive();
}

void OpenAiCompatClient::abortActive()
{
    if (!m_reply)
        return;
    m_reply->disconnect(this);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
    m_sseBuffer.clear();
}

QString OpenAiCompatClient::buildCompletionsUrl(const QString& baseUrl)
{
    QString s = baseUrl.trimmed();
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    const QString suffix = QStringLiteral("/chat/completions");
    if (s.endsWith(suffix))
        return s;
    return s + suffix;
}

void OpenAiCompatClient::requestChatCompletion(const QString& completionsUrl,
                                               const QString& apiKey,
                                               const QString& model,
                                               const QJsonArray& messages,
                                               bool stream)
{
    abortActive();
    m_streamMode = stream;
    m_sseBuffer.clear();

    QUrl u(completionsUrl);
    if (!u.isValid() || u.scheme().isEmpty()) {
        emit failed(QStringLiteral("无效的 API 地址"));
        return;
    }

    QJsonObject root;
    root[QStringLiteral("model")] = model;
    root[QStringLiteral("messages")] = messages;
    root[QStringLiteral("stream")] = stream;

    const QJsonDocument doc(root);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest req(u);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey.toUtf8());

    m_reply = m_nam->post(req, body);
    if (!m_reply) {
        emit failed(QStringLiteral("无法发起网络请求"));
        return;
    }

    connect(m_reply, &QNetworkReply::readyRead, this, &OpenAiCompatClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &OpenAiCompatClient::onReplyFinished);
}

void OpenAiCompatClient::onReadyRead()
{
    if (!m_reply || !m_streamMode)
        return;
    m_sseBuffer += m_reply->readAll();
    processSseBuffer();
}

void OpenAiCompatClient::processSseBuffer()
{
    for (;;) {
        const int nl = m_sseBuffer.indexOf('\n');
        if (nl < 0)
            break;
        QByteArray line = m_sseBuffer.left(nl);
        m_sseBuffer.remove(0, nl + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (!handleSseLine(line))
            return;
    }
}

bool OpenAiCompatClient::handleSseLine(const QByteArray& line)
{
    if (line.trimmed().isEmpty())
        return true;
    if (line.startsWith(':'))
        return true;

    QByteArray payload;
    if (!jsonLineContent(line, &payload))
        return true;

    if (payload == "[DONE]")
        return true;

    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !jd.isObject())
        return true;

    const QJsonObject obj = jd.object();
    const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return true;
    const QJsonObject c0 = choices.at(0).toObject();
    const QJsonObject delta = c0.value(QStringLiteral("delta")).toObject();
    const QString piece = delta.value(QStringLiteral("content")).toString();
    if (!piece.isEmpty())
        emit streamDelta(piece);

    const QJsonObject msg = c0.value(QStringLiteral("message")).toObject();
    const QString whole = msg.value(QStringLiteral("content")).toString();
    if (!whole.isEmpty())
        emit streamDelta(whole);

    return true;
}

void OpenAiCompatClient::onReplyFinished()
{
    if (!m_reply)
        return;

    QNetworkReply* reply = m_reply;
    m_reply = nullptr;

    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray tail = reply->readAll();
    if (m_streamMode && !tail.isEmpty())
        m_sseBuffer += tail;

    const auto done = [reply]() { reply->deleteLater(); };

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            done();
            return;
        }
        QString reason = reply->errorString();
        if (code > 0)
            reason = QStringLiteral("HTTP %1 %2").arg(code).arg(reason);
        emit failed(reason);
        done();
        return;
    }

    const bool httpOk = (code >= 200 && code < 300);
    if (!httpOk) {
        QString reason = QStringLiteral("HTTP %1").arg(code > 0 ? code : 0);
        const QString apiMsg = extractErrorMessageFromJson(tail.isEmpty() ? m_sseBuffer : tail);
        if (!apiMsg.isEmpty())
            reason = apiMsg;
        emit failed(reason);
        done();
        return;
    }

    if (m_streamMode) {
        processSseBuffer();
        emit completed();
        done();
        return;
    }

    const QByteArray body = tail;
    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !jd.isObject()) {
        emit failed(QStringLiteral("响应解析失败"));
        done();
        return;
    }
    const QJsonObject root = jd.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        emit completed();
        done();
        return;
    }
    const QJsonObject c0 = choices.at(0).toObject();
    const QJsonObject message = c0.value(QStringLiteral("message")).toObject();
    const QString content = message.value(QStringLiteral("content")).toString();
    if (!content.isEmpty())
        emit streamDelta(content);
    emit completed();
    done();
}
