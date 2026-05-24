#include "wechatworkbenchservice.h"

#include "../ai/aiprovidercatalog.h"
#include "../ai/aiservicefacade.h"
#include "../ai/aistreamingsession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcessEnvironment>

namespace {

QString decodeProcessText(const QByteArray& bytes)
{
    QString text = QString::fromUtf8(bytes);
    if (text.contains(QChar::ReplacementCharacter))
        text = QString::fromLocal8Bit(bytes);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QStringLiteral(""));
    return text;
}

QString sideDisplayLabel(const QString& side, const QString& senderName)
{
    const QString sender = senderName.trimmed();
    if (!sender.isEmpty())
        return sender;
    if (side == QLatin1String("incoming"))
        return QStringLiteral("客户");
    if (side == QLatin1String("outgoing"))
        return QStringLiteral("我方");
    return QStringLiteral("未知方向");
}

} // namespace

WeChatWorkbenchService::WeChatWorkbenchService(QObject* parent)
    : QObject(parent)
    , m_aiNam(new QNetworkAccessManager(this))
    , m_aiService(new AiServiceFacade(m_aiNam, this))
{
}

WeChatWorkbenchService::~WeChatWorkbenchService()
{
    stopProcess();
}

int WeChatWorkbenchService::probeStatus()
{
    return sendCommand(QStringLiteral("probe_status"));
}

int WeChatWorkbenchService::listSessions()
{
    return sendCommand(QStringLiteral("list_sessions"));
}

int WeChatWorkbenchService::switchSession(const QString& sessionName)
{
    return sendCommand(QStringLiteral("switch_session"),
                       QJsonObject{{QStringLiteral("session"), sessionName.trimmed()}});
}

int WeChatWorkbenchService::readCurrentMessages(const QString& sessionName)
{
    QJsonObject args;
    if (!sessionName.trimmed().isEmpty())
        args.insert(QStringLiteral("session"), sessionName.trimmed());
    return sendCommand(QStringLiteral("read_current_messages"), args);
}

int WeChatWorkbenchService::sendText(const QString& sessionName, const QString& text)
{
    return sendCommand(QStringLiteral("send_text"),
                       QJsonObject{
                           {QStringLiteral("session"), sessionName.trimmed()},
                           {QStringLiteral("text"), text},
                       });
}

void WeChatWorkbenchService::stopProcess()
{
    abortAiSuggestion();
    const auto pending = m_pendingCommands;
    m_pendingCommands.clear();
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
        emit commandFailed(it.key(), it.value(), QStringLiteral("Python 服务已停止"));

    if (!m_process)
        return;

    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1500);
        emit processLogAppended(QStringLiteral("[微信工作台] 已手动停止 Python 服务"));
    }
    m_process->deleteLater();
    m_process = nullptr;
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    notifyPythonServiceActiveChanged();
}

bool WeChatWorkbenchService::isPythonServiceActive() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool WeChatWorkbenchService::startPythonService(QString* errorOut)
{
    return spawnPythonProcess(errorOut);
}

void WeChatWorkbenchService::notifyPythonServiceActiveChanged()
{
    emit pythonServiceActiveChanged(isPythonServiceActive());
}

void WeChatWorkbenchService::requestAiSuggestion(const QString& sessionName,
                                                 const QJsonArray& messages,
                                                 const QString& sessionModelKey)
{
    clearAiSession();

    const AiProviderConfig config = loadAiProviderConfig(sessionModelKey);
    if (config.apiKey.trimmed().isEmpty()) {
        emit aiSuggestionFailed(
            QStringLiteral("缺少 API Key，请先在左栏「管理后台」→「AI 客服后台」→「API 配置/模型」中配置并保存。"));
        return;
    }
    if (config.baseUrl.trimmed().isEmpty() || config.model.trimmed().isEmpty()) {
        emit aiSuggestionFailed(QStringLiteral("模型配置不完整，请检查 Base URL 与模型名称。"));
        return;
    }

    AiRequest request;
    request.systemPrompt = buildAiSystemPrompt();
    request.turns.append(makeAiTextTurn(QStringLiteral("user"), buildTranscript(sessionName, messages)));
    request.extraRootFields = QJsonObject{{QStringLiteral("temperature"), 0.4}};

    m_aiAccumulated.clear();
    m_aiBusy = true;
    emit aiSuggestionStarted();
    m_aiSession = m_aiService->createSession(config, request, this);
    connect(m_aiSession, &IAiStreamingSession::delta, this, &WeChatWorkbenchService::onAiClientDelta);
    connect(m_aiSession, &IAiStreamingSession::completed, this, &WeChatWorkbenchService::onAiClientCompleted);
    connect(m_aiSession, &IAiStreamingSession::failed, this, &WeChatWorkbenchService::onAiClientFailed);
    m_aiSession->start();
}

void WeChatWorkbenchService::abortAiSuggestion()
{
    clearAiSession();
    m_aiBusy = false;
    m_aiAccumulated.clear();
}

void WeChatWorkbenchService::onProcessStdoutReady()
{
    if (!m_process)
        return;
    m_stdoutBuffer += m_process->readAllStandardOutput();
    processStdoutBuffer();
}

void WeChatWorkbenchService::onProcessStderrReady()
{
    if (!m_process)
        return;
    m_stderrBuffer += m_process->readAllStandardError();
    processStderrBuffer();
}

void WeChatWorkbenchService::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)
    processStdoutBuffer();
    processStderrBuffer();
    emit processLogAppended(QStringLiteral("[微信工作台] Python 服务已退出（exit=%1）").arg(exitCode));

    const auto pending = m_pendingCommands;
    m_pendingCommands.clear();
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
        emit commandFailed(it.key(), it.value(), QStringLiteral("Python 服务已退出"));

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    notifyPythonServiceActiveChanged();
}

void WeChatWorkbenchService::onProcessError(QProcess::ProcessError error)
{
    emit processLogAppended(
        QStringLiteral("[微信工作台] Python 服务进程错误 code=%1").arg(int(error)));
}

void WeChatWorkbenchService::onAiClientDelta(const QString& delta)
{
    m_aiAccumulated += delta;
    emit aiSuggestionDelta(delta);
}

void WeChatWorkbenchService::onAiClientCompleted()
{
    const QString text = m_aiAccumulated.trimmed();
    m_aiBusy = false;
    clearAiSession();
    emit aiSuggestionCompleted(text);
}

void WeChatWorkbenchService::onAiClientFailed(const QString& reason)
{
    m_aiBusy = false;
    clearAiSession();
    emit aiSuggestionFailed(reason);
}

bool WeChatWorkbenchService::ensurePythonServiceRunning(QString* error)
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return true;

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (error)
        *error = QStringLiteral("请先点击左侧「启动 Python 服务」后再操作。");
    return false;
}

bool WeChatWorkbenchService::spawnPythonProcess(QString* error)
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return true;

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    auto* proc = new QProcess(this);
    proc->setProgram(QStringLiteral("python"));
    proc->setArguments(QStringList()
                       << QStringLiteral("-u")
                       << QStringLiteral("rpa/tools/wechat_workbench_service.py"));
    proc->setWorkingDirectory(QStringLiteral(PROJECT_ROOT_DIR) + QStringLiteral("/python"));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    proc->setProcessEnvironment(env);
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    connect(proc, &QProcess::readyReadStandardOutput, this, &WeChatWorkbenchService::onProcessStdoutReady);
    connect(proc, &QProcess::readyReadStandardError, this, &WeChatWorkbenchService::onProcessStderrReady);
    connect(proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &WeChatWorkbenchService::onProcessFinished);
    connect(proc, &QProcess::errorOccurred, this, &WeChatWorkbenchService::onProcessError);

    proc->start();
    if (!proc->waitForStarted(3000)) {
        const QString reason = QStringLiteral("无法启动 Python 服务，请确认 python 命令可用。");
        if (error)
            *error = reason;
        proc->deleteLater();
        return false;
    }

    m_process = proc;
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    emit processLogAppended(QStringLiteral("[微信工作台] 已启动 Python 服务"));
    notifyPythonServiceActiveChanged();
    return true;
}

int WeChatWorkbenchService::sendCommand(const QString& cmd, const QJsonObject& args)
{
    QString error;
    if (!ensurePythonServiceRunning(&error)) {
        const int failedId = m_nextRequestId++;
        emit commandFailed(failedId, cmd, error);
        return failedId;
    }

    const int requestId = m_nextRequestId++;
    m_pendingCommands.insert(requestId, cmd);

    QJsonObject root{
        {QStringLiteral("id"), requestId},
        {QStringLiteral("cmd"), cmd},
        {QStringLiteral("args"), args},
    };
    const QByteArray line = QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
    if (m_process->write(line) < 0) {
        m_pendingCommands.remove(requestId);
        emit commandFailed(requestId, cmd, QStringLiteral("无法向 Python 服务写入请求。"));
    }
    m_process->waitForBytesWritten(100);
    return requestId;
}

void WeChatWorkbenchService::processStdoutBuffer()
{
    for (;;) {
        const int nl = m_stdoutBuffer.indexOf('\n');
        if (nl < 0)
            break;
        QByteArray line = m_stdoutBuffer.left(nl);
        m_stdoutBuffer.remove(0, nl + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        handleProtocolLine(line);
    }
}

void WeChatWorkbenchService::processStderrBuffer()
{
    for (;;) {
        const int nl = m_stderrBuffer.indexOf('\n');
        if (nl < 0)
            break;
        QByteArray line = m_stderrBuffer.left(nl);
        m_stderrBuffer.remove(0, nl + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        const QString text = decodeProcessText(line).trimmed();
        if (!text.isEmpty())
            emit processLogAppended(text);
    }
}

void WeChatWorkbenchService::handleProtocolLine(const QByteArray& line)
{
    const QString plain = decodeProcessText(line).trimmed();
    if (plain.isEmpty())
        return;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit processLogAppended(plain);
        return;
    }

    const QJsonObject root = doc.object();
    const int requestId = root.value(QStringLiteral("id")).toInt(-1);
    QString cmd = root.value(QStringLiteral("cmd")).toString();
    if (cmd.isEmpty() && requestId >= 0)
        cmd = m_pendingCommands.value(requestId);
    if (requestId >= 0)
        m_pendingCommands.remove(requestId);

    const bool ok = root.value(QStringLiteral("ok")).toBool(false);
    if (!ok) {
        emit commandFailed(requestId, cmd, root.value(QStringLiteral("error")).toString());
        return;
    }
    emit commandSucceeded(requestId, cmd, root.value(QStringLiteral("data")).toObject());
}

void WeChatWorkbenchService::clearAiSession()
{
    if (!m_aiSession)
        return;
    m_aiSession->disconnect(this);
    m_aiSession->abort();
    m_aiSession->deleteLater();
    m_aiSession = nullptr;
}

QString WeChatWorkbenchService::buildAiSystemPrompt() const
{
    return QStringLiteral(
        "你是微信客服助手。请基于给出的当前会话可见消息，生成一条可直接发送给客户的中文简体回复建议。"
        "要求：1）回复简洁、礼貌、自然；2）优先回应最后一条客户相关消息；3）如果上下文不足，给出保守澄清式回复；"
        "4）不要输出分析过程、标题、Markdown、引号或多条候选。");
}

QString WeChatWorkbenchService::buildTranscript(const QString& sessionName, const QJsonArray& messages) const
{
    QStringList lines;
    lines.append(QStringLiteral("当前会话：%1").arg(sessionName.trimmed().isEmpty() ? QStringLiteral("未命名会话")
                                                                          : sessionName.trimmed()));
    lines.append(QStringLiteral("以下为当前窗口可见消息（顺序从旧到新，方向可能存在 unknown）："));

    const int start = qMax(0, messages.size() - 20);
    for (int i = start; i < messages.size(); ++i) {
        const QJsonObject msg = messages.at(i).toObject();
        const QString content = msg.value(QStringLiteral("content")).toString().trimmed();
        if (content.isEmpty())
            continue;
        const QString side = msg.value(QStringLiteral("side")).toString().trimmed();
        const QString sender = msg.value(QStringLiteral("sender_name")).toString().trimmed();
        lines.append(QStringLiteral("- [%1] %2")
                         .arg(sideDisplayLabel(side, sender), content));
    }
    lines.append(QStringLiteral("请直接输出建议回复正文。"));
    return lines.join(QLatin1Char('\n'));
}
