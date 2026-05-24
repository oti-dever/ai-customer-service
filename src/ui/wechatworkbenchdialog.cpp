#include "wechatworkbenchdialog.h"

#include "../services/wechat/wechatworkbenchservice.h"
#include "../utils/appsettings.h"
#include "../utils/applystyle.h"
#include "../utils/win32windowhelper.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QPainter>
#include <QCloseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include <QSplitter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

class WeChatSessionListDelegate final : public QStyledItemDelegate
{
public:
    explicit WeChatSessionListDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        opt.state &= ~QStyle::State_HasFocus;
        opt.state &= ~QStyle::State_KeyboardFocusChange;
        QStyledItemDelegate::paint(painter, opt, index);
    }
};

QString displaySideLabel(const QString& side)
{
    if (side == QLatin1String("incoming"))
        return QStringLiteral("收到");
    if (side == QLatin1String("outgoing"))
        return QStringLiteral("发出");
    return QStringLiteral("未知");
}

struct BubblePalette {
    QString bubbleBg;
    QString bubbleBorder;
    QString textColor;
    QString metaColor;
    QString tagBg;
    QString tagText;
};

BubblePalette bubblePaletteFor(ApplyStyle::MainWindowTheme theme, const QString& side)
{
    const bool useDefault = theme == ApplyStyle::MainWindowTheme::Default;
    if (side == QLatin1String("incoming")) {
        return {
            useDefault ? QStringLiteral("#eef6ff") : QStringLiteral("#edf6ff"),
            useDefault ? QStringLiteral("#bfdbfe") : QStringLiteral("#b8d6f5"),
            QStringLiteral("#0f172a"),
            QStringLiteral("#475569"),
            useDefault ? QStringLiteral("#dbeafe") : QStringLiteral("#d8ecff"),
            QStringLiteral("#075985"),
        };
    }
    if (side == QLatin1String("outgoing")) {
        return {
            useDefault ? QStringLiteral("#eefdf3") : QStringLiteral("#edf9f0"),
            useDefault ? QStringLiteral("#bbf7d0") : QStringLiteral("#bfdcc8"),
            QStringLiteral("#0f172a"),
            QStringLiteral("#475569"),
            useDefault ? QStringLiteral("#dcfce7") : QStringLiteral("#dcefe2"),
            QStringLiteral("#166534"),
        };
    }
    return {
        useDefault ? QStringLiteral("#f8fafc") : QStringLiteral("#f7f9fc"),
        useDefault ? QStringLiteral("#dbe4ee") : QStringLiteral("#d6e0ec"),
        QStringLiteral("#0f172a"),
        QStringLiteral("#64748b"),
        useDefault ? QStringLiteral("#e2e8f0") : QStringLiteral("#e5ebf2"),
        QStringLiteral("#475569"),
    };
}

QWidget* createMessageBubbleWidget(const QJsonObject& msg,
                                   ApplyStyle::MainWindowTheme theme,
                                   QWidget* parent)
{
    const QString side = msg.value(QStringLiteral("side")).toString().trimmed();
    const QString sender = msg.value(QStringLiteral("sender_name")).toString().trimmed();
    const QString content = msg.value(QStringLiteral("content")).toString().trimmed();
    const QString kind = msg.value(QStringLiteral("kind")).toString().trimmed();
    const QString directionSource = msg.value(QStringLiteral("direction_source")).toString().trimmed();
    const BubblePalette pal = bubblePaletteFor(theme, side);

    auto* outer = new QWidget(parent);
    auto* outerLayout = new QHBoxLayout(outer);
    outerLayout->setContentsMargins(6, 6, 6, 6);
    outerLayout->setSpacing(0);

    auto* bubble = new QFrame(outer);
    bubble->setObjectName(QStringLiteral("wechatWorkbenchBubble"));
    bubble->setMinimumWidth(220);
    bubble->setMaximumWidth(560);
    bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    bubble->setStyleSheet(
        QStringLiteral(
            "QFrame#wechatWorkbenchBubble {"
            "background:%1; border:1px solid %2; border-radius:12px;"
            "}"
            "QLabel { background: transparent; }")
            .arg(pal.bubbleBg, pal.bubbleBorder));

    auto* bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(12, 10, 12, 10);
    bubbleLayout->setSpacing(6);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);

    auto* tag = new QLabel(displaySideLabel(side), bubble);
    tag->setStyleSheet(
        QStringLiteral(
            "QLabel { background:%1; color:%2; border-radius:9px; padding:2px 8px; font-weight:600; }")
            .arg(pal.tagBg, pal.tagText));
    topRow->addWidget(tag, 0, Qt::AlignLeft);

    QString metaText = sender;
    if (metaText.isEmpty())
        metaText = displaySideLabel(side);
    if (!kind.isEmpty() && kind != QLatin1String("text"))
        metaText += QStringLiteral(" · %1").arg(kind);
    if (!directionSource.isEmpty() && directionSource != QLatin1String("disabled"))
        metaText += QStringLiteral(" · %1").arg(directionSource);
    auto* meta = new QLabel(metaText, bubble);
    meta->setStyleSheet(QStringLiteral("QLabel { color:%1; font-size:12px; }").arg(pal.metaColor));
    topRow->addWidget(meta, 1);
    bubbleLayout->addLayout(topRow);

    auto* contentLabel = new QLabel(content.isEmpty() ? QStringLiteral("（空消息）") : content, bubble);
    contentLabel->setWordWrap(true);
    contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    contentLabel->setStyleSheet(
        QStringLiteral("QLabel { color:%1; font-size:14px; line-height:1.45; }").arg(pal.textColor));
    bubbleLayout->addWidget(contentLabel);

    if (side == QLatin1String("outgoing")) {
        outerLayout->addStretch(1);
        outerLayout->addWidget(bubble, 0, Qt::AlignRight);
    } else if (side == QLatin1String("incoming")) {
        outerLayout->addWidget(bubble, 0, Qt::AlignLeft);
        outerLayout->addStretch(1);
    } else {
        outerLayout->addStretch(1);
        outerLayout->addWidget(bubble, 0, Qt::AlignHCenter);
        outerLayout->addStretch(1);
    }
    return outer;
}

QIcon loadResourceIcon(const QString& path, const QIcon& fallback = {})
{
    const QIcon icon(path);
    return icon.isNull() ? fallback : icon;
}

QToolButton* makeIconToolButton(QWidget* parent, const QIcon& icon, const QString& toolTip)
{
    auto* button = new QToolButton(parent);
    button->setIcon(icon);
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setIconSize(QSize(18, 18));
    return button;
}

void applyWorkbenchSessionHeaderToolButtonSize(QToolButton* button)
{
    if (!button)
        return;
    button->setIconSize(QSize(24, 24));
    button->setMinimumSize(40, 40);
}

} // namespace

WeChatWorkbenchDialog::WeChatWorkbenchDialog(QWidget* parent)
    : QDialog(parent)
    , m_service(new WeChatWorkbenchService(this))
{
    setWindowFlag(Qt::Window, true);
    setWindowModality(Qt::NonModal);
    setWindowTitle(QStringLiteral("微信 RPA 工作台"));
    resize(860, 640);
    setupUi();
    {
        QSettings pinSettings = AppSettings::create();
        m_alwaysOnTop = pinSettings.value(QStringLiteral("wechatWorkbench/alwaysOnTop"), false).toBool();
    }
    updatePinTopButtonUi();
    applyTheme(m_theme);

    connect(m_service, &WeChatWorkbenchService::commandSucceeded, this,
            &WeChatWorkbenchDialog::onCommandSucceeded);
    connect(m_service, &WeChatWorkbenchService::commandFailed, this,
            &WeChatWorkbenchDialog::onCommandFailed);
    connect(m_service, &WeChatWorkbenchService::aiSuggestionStarted, this,
            &WeChatWorkbenchDialog::onAiSuggestionStarted);
    connect(m_service, &WeChatWorkbenchService::aiSuggestionDelta, this,
            &WeChatWorkbenchDialog::onAiSuggestionDelta);
    connect(m_service, &WeChatWorkbenchService::aiSuggestionCompleted, this,
            &WeChatWorkbenchDialog::onAiSuggestionCompleted);
    connect(m_service, &WeChatWorkbenchService::aiSuggestionFailed, this,
            &WeChatWorkbenchDialog::onAiSuggestionFailed);
    connect(m_service, &WeChatWorkbenchService::processLogAppended, this,
            &WeChatWorkbenchDialog::appendProcessLog);
    connect(m_service, &WeChatWorkbenchService::pythonServiceActiveChanged, this,
            &WeChatWorkbenchDialog::updateScriptServiceButtonUi);
    updateScriptServiceButtonUi();
}

void WeChatWorkbenchDialog::applyTheme(ApplyStyle::MainWindowTheme theme)
{
    m_theme = theme;
    const QString accent = (theme == ApplyStyle::MainWindowTheme::Default)
        ? QStringLiteral("#3b82f6")
        : QStringLiteral("#2f7fd6");
    const QString border = (theme == ApplyStyle::MainWindowTheme::Default)
        ? QStringLiteral("#d7dde6")
        : QStringLiteral("#c0d9f7");
    const QString softBg = (theme == ApplyStyle::MainWindowTheme::Default)
        ? QStringLiteral("#f8fbff")
        : QStringLiteral("#f3f8ff");
    const QString panelBg = (theme == ApplyStyle::MainWindowTheme::Default)
        ? QStringLiteral("#f3f7fd")
        : QStringLiteral("#eef5ff");
    const QString textMuted = (theme == ApplyStyle::MainWindowTheme::Default)
        ? QStringLiteral("#64748b")
        : QStringLiteral("#5a6f86");
    const QString accentHover = (theme == ApplyStyle::MainWindowTheme::Default)
        ? QStringLiteral("#2563eb")
        : QStringLiteral("#256cad");
    const QString accentPressed = (theme == ApplyStyle::MainWindowTheme::Default)
        ? QStringLiteral("#1d4ed8")
        : QStringLiteral("#1e5aa8");
    const QString sessionListHoverBg = (theme == ApplyStyle::MainWindowTheme::Default)
        ? QStringLiteral("#e8f1fc")
        : QStringLiteral("#e2ebf7");
    const QString sessionListSelectedHoverBg = (theme == ApplyStyle::MainWindowTheme::Default)
        ? QStringLiteral("#dbeafe")
        : QStringLiteral("#d4e4f5");

    setStyleSheet(
        ApplyStyle::addWindowDialogStyle(theme)
        + QStringLiteral(
              R"QSS(
        QDialog {
            background: #eef4fb;
        }
        QWidget#wechatWorkbenchEnvHintWrap {
            background: transparent;
        }
        QLabel#wechatWorkbenchStatusLabel {
            font-weight: 700;
            color: #0f172a;
            padding: 6px 10px;
            border: 1px solid %1;
            border-radius: 8px;
            background: %2;
        }
        QLabel#wechatWorkbenchStatusLabel[errorState="true"] {
            color: #991b1b;
            border-color: #fecaca;
            background: #fff1f2;
        }
        QLabel#wechatWorkbenchEnvHintLabel {
            color: %5;
            padding: 0px 2px 0px 0px;
            font-size: 12px;
        }
        QSplitter#wechatWorkbenchMainSplitter::handle:horizontal {
            width: 2px;
            background: #eef4fb;
        }
        QWidget#wechatWorkbenchLeftPanel,
        QWidget#wechatWorkbenchRightPanel,
        QWidget#wechatWorkbenchChatHeader,
        QWidget#wechatWorkbenchComposerPanel {
            background: #ffffff;
            border: 1px solid %1;
            border-radius: 12px;
        }
        QWidget#wechatWorkbenchChatHeader,
        QWidget#wechatWorkbenchComposerPanel {
            background: %4;
        }
        QLabel#wechatWorkbenchPaneTitle,
        QLabel#wechatWorkbenchSectionTitle,
        QLabel#wechatWorkbenchChatTitle {
            color: #0f172a;
            font-weight: 700;
        }
        QLabel#wechatWorkbenchChatMeta {
            color: %5;
            font-size: 12px;
        }
        QListWidget#wechatWorkbenchSessionList,
        QListWidget#wechatWorkbenchMessageList,
        QPlainTextEdit#wechatWorkbenchSuggestionEdit {
            border: 1px solid %1;
            border-radius: 8px;
            background: #ffffff;
            color: #0f172a;
            selection-color: #0f172a;
            selection-background-color: #dbeafe;
        }
        QPlainTextEdit#wechatWorkbenchReplyEdit,
        QPlainTextEdit#wechatWorkbenchLogEdit {
            border: 1px solid %1;
            border-radius: 12px;
            background: #ffffff;
            color: #0f172a;
            selection-color: #0f172a;
            selection-background-color: #dbeafe;
        }
        QListWidget#wechatWorkbenchMessageList {
            padding: 4px;
            color: #0f172a;
        }
        QListWidget#wechatWorkbenchSessionList {
            padding: 4px;
            color: #0f172a;
            outline: none;
        }
        QListWidget#wechatWorkbenchSessionList:focus {
            outline: none;
        }
        QListWidget#wechatWorkbenchSessionList::item {
            border: none;
            outline: none;
            border-radius: 10px;
            padding: 10px 12px;
            margin: 2px 0px;
            font-size: 18px;
        }
        QListWidget#wechatWorkbenchSessionList::item:hover {
            background: %8;
            color: #0f172a;
        }
        QListWidget#wechatWorkbenchSessionList::item:selected {
            background: %2;
            color: #0f172a;
            border: none;
        }
        QListWidget#wechatWorkbenchSessionList::item:selected:hover {
            background: %9;
            color: #0f172a;
        }
        QListWidget#wechatWorkbenchSessionList::item:focus {
            border: none;
            outline: none;
        }
        QListWidget#wechatWorkbenchMessageList::item:selected {
            background: %2;
            color: #0f172a;
        }
        QPushButton#wechatWorkbenchPrimaryButton {
            background: %3;
            color: white;
            border: none;
            border-radius: 8px;
            padding: 8px 16px;
            min-height: 22px;
        }
        QPushButton#wechatWorkbenchPrimaryButton:hover {
            background: %6;
        }
        QPushButton#wechatWorkbenchPrimaryButton:pressed {
            background: %7;
        }
    )QSS")
              .arg(border, softBg, accent, panelBg, textMuted, accentHover, accentPressed,
                   sessionListHoverBg, sessionListSelectedHoverBg));
}

void WeChatWorkbenchDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (m_alwaysOnTop)
        Win32WindowHelper::applyNativeTopMost(this, true);
    m_firstShow = false;
    if (m_service && m_service->isPythonServiceActive())
        refreshAll();
    else
        updateStatusBanner(QStringLiteral("请先点击左侧「启动 Python 服务」以连接微信 UIA。"));
}

void WeChatWorkbenchDialog::closeEvent(QCloseEvent* event)
{
    if (m_service)
        m_service->stopProcess();
    QDialog::closeEvent(event);
}

bool WeChatWorkbenchDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_replyEdit && event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if ((keyEvent->modifiers() & Qt::ControlModifier)
            && (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)) {
            onSendClicked();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void WeChatWorkbenchDialog::updatePinTopButtonUi()
{
    if (!m_pinTopBtn)
        return;
    if (m_alwaysOnTop) {
        m_pinTopBtn->setIcon(loadResourceIcon(QStringLiteral(":/after_pinning.svg")));
        m_pinTopBtn->setToolTip(QStringLiteral("取消置顶"));
    } else {
        m_pinTopBtn->setIcon(loadResourceIcon(QStringLiteral(":/before_pinning.svg")));
        m_pinTopBtn->setToolTip(QStringLiteral("置顶"));
    }
}

void WeChatWorkbenchDialog::applyAlwaysOnTop(bool on)
{
    if (m_alwaysOnTop == on)
        return;
    m_alwaysOnTop = on;
    Win32WindowHelper::applyNativeTopMost(this, on);
    QSettings pinSettings = AppSettings::create();
    pinSettings.setValue(QStringLiteral("wechatWorkbench/alwaysOnTop"), m_alwaysOnTop);
    updatePinTopButtonUi();
}

void WeChatWorkbenchDialog::onPinTopClicked()
{
    applyAlwaysOnTop(!m_alwaysOnTop);
}

void WeChatWorkbenchDialog::onEnvHintCloseClicked()
{
    if (m_envHintContainer)
        m_envHintContainer->setVisible(false);
}

void WeChatWorkbenchDialog::onRefreshSessionsClicked()
{
    refreshAll();
}

void WeChatWorkbenchDialog::onRefreshMessagesClicked()
{
    m_service->readCurrentMessages();
}

void WeChatWorkbenchDialog::onGenerateSuggestionClicked()
{
    if (m_currentMessages.isEmpty()) {
        updateStatusBanner(QStringLiteral("当前没有可用于 AI 建议回复的可见消息。"), true);
        return;
    }
    m_service->requestAiSuggestion(m_currentConversationName, m_currentMessages, currentPresetKey());
}

void WeChatWorkbenchDialog::onSendClicked()
{
    const QString session = currentSessionName();
    const QString text = m_replyEdit ? m_replyEdit->toPlainText().trimmed() : QString();
    if (session.isEmpty()) {
        updateStatusBanner(QStringLiteral("请先选择一个微信会话。"), true);
        return;
    }
    if (text.isEmpty()) {
        updateStatusBanner(QStringLiteral("发送内容不能为空。"), true);
        return;
    }
    m_sendBtn->setEnabled(false);
    m_service->sendText(session, text);
}

void WeChatWorkbenchDialog::updateScriptServiceButtonUi()
{
    if (!m_scriptServiceBtn || !m_service)
        return;
    if (m_service->isPythonServiceActive()) {
        m_scriptServiceBtn->setIcon(
            loadResourceIcon(QStringLiteral(":/stop_script_icon.svg"),
                             qApp->style()->standardIcon(QStyle::SP_MediaStop)));
        m_scriptServiceBtn->setToolTip(QStringLiteral("停止脚本"));
    } else {
        m_scriptServiceBtn->setIcon(
            loadResourceIcon(QStringLiteral(":/startup_script_icon.svg"),
                             qApp->style()->standardIcon(QStyle::SP_MediaPlay)));
        m_scriptServiceBtn->setToolTip(QStringLiteral("启动 Python 服务"));
    }
}

void WeChatWorkbenchDialog::onScriptServiceControlClicked()
{
    if (!m_service)
        return;
    if (m_service->isPythonServiceActive()) {
        m_service->stopProcess();
        updateStatusBanner(QStringLiteral("已停止微信工作台后台脚本。"));
    } else {
        QString err;
        if (!m_service->startPythonService(&err)) {
            updateStatusBanner(err.isEmpty() ? QStringLiteral("无法启动 Python 服务。") : err, true);
            return;
        }
        updateStatusBanner(QStringLiteral("Python 服务已启动。"));
        refreshAll();
    }
}

void WeChatWorkbenchDialog::onSessionItemActivated(QListWidgetItem* item)
{
    if (m_updatingSessionList || !item)
        return;
    const QString session = item->data(Qt::UserRole).toString().trimmed();
    if (session.isEmpty())
        return;
    m_currentConversationName = session;
    refreshConversationHeader(QStringLiteral("正在切换会话并读取当前可见消息..."));
    updateStatusBanner(QStringLiteral("正在切换到会话：%1").arg(session));
    m_service->switchSession(session);
}

void WeChatWorkbenchDialog::onCommandSucceeded(int requestId, const QString& cmd, const QJsonObject& data)
{
    Q_UNUSED(requestId)
    if (cmd == QLatin1String("probe_status")) {
        const QJsonObject probe = data.value(QStringLiteral("probe")).toObject();
        const QString reason = probe.value(QStringLiteral("reason")).toString();
        const QString envHint = data.value(QStringLiteral("env_hint")).toString();
        if (!envHint.isEmpty()) {
            m_envHintLabel->setText(envHint);
            if (m_envHintContainer)
                m_envHintContainer->setVisible(true);
        } else {
            m_envHintLabel->clear();
            if (m_envHintContainer)
                m_envHintContainer->setVisible(false);
        }
        updateStatusBanner(reason.isEmpty() ? QStringLiteral("微信 UIA 环境检查完成。") : reason,
                           !probe.value(QStringLiteral("available")).toBool());
        return;
    }

    if (cmd == QLatin1String("list_sessions")) {
        populateSessions(data);
        const QString current = data.value(QStringLiteral("current_session")).toString().trimmed();
        if (!current.isEmpty())
            m_service->readCurrentMessages();
        return;
    }

    if (cmd == QLatin1String("switch_session")) {
        const QString current = data.value(QStringLiteral("current_session")).toString().trimmed();
        if (!current.isEmpty())
            m_currentConversationName = current;
        updateStatusBanner(QStringLiteral("已切换到会话：%1").arg(m_currentConversationName));
        m_service->readCurrentMessages();
        return;
    }

    if (cmd == QLatin1String("read_current_messages")) {
        populateMessages(data);
        return;
    }

    if (cmd == QLatin1String("send_text")) {
        m_sendBtn->setEnabled(true);
        if (m_replyEdit)
            m_replyEdit->clear();
        const QString current = data.value(QStringLiteral("current_session")).toString().trimmed();
        if (!current.isEmpty())
            m_currentConversationName = current;
        updateStatusBanner(QStringLiteral("消息已发送到微信。"));
        m_service->readCurrentMessages();
        return;
    }
}

void WeChatWorkbenchDialog::onCommandFailed(int requestId, const QString& cmd, const QString& reason)
{
    Q_UNUSED(requestId)
    if (cmd == QLatin1String("send_text"))
        m_sendBtn->setEnabled(true);
    updateStatusBanner(
        QStringLiteral("%1 失败：%2").arg(cmd.isEmpty() ? QStringLiteral("请求") : cmd,
                                         reason.isEmpty() ? QStringLiteral("未知错误") : reason),
        true);
}

void WeChatWorkbenchDialog::onAiSuggestionStarted()
{
    m_generateSuggestionBtn->setEnabled(false);
    if (m_replyEdit)
        m_replyEdit->clear();
    updateStatusBanner(QStringLiteral("正在生成 AI 建议回复..."));
}

void WeChatWorkbenchDialog::onAiSuggestionDelta(const QString& delta)
{
    if (delta.isEmpty())
        return;
    if (!m_replyEdit)
        return;
    m_replyEdit->moveCursor(QTextCursor::End);
    m_replyEdit->insertPlainText(delta);
    m_replyEdit->moveCursor(QTextCursor::End);
}

void WeChatWorkbenchDialog::onAiSuggestionCompleted(const QString& text)
{
    m_generateSuggestionBtn->setEnabled(true);
    updateStatusBanner(text.trimmed().isEmpty() ? QStringLiteral("AI 未返回建议回复。")
                                                : QStringLiteral("AI 建议回复已生成。"),
                       text.trimmed().isEmpty());
}

void WeChatWorkbenchDialog::onAiSuggestionFailed(const QString& reason)
{
    m_generateSuggestionBtn->setEnabled(true);
    updateStatusBanner(QStringLiteral("AI 建议回复失败：%1").arg(reason), true);
}

void WeChatWorkbenchDialog::appendProcessLog(const QString& text)
{
    if (!m_logEdit || text.trimmed().isEmpty())
        return;
    m_logEdit->moveCursor(QTextCursor::End);
    m_logEdit->insertPlainText(text);
    if (!text.endsWith(QLatin1Char('\n')))
        m_logEdit->insertPlainText(QStringLiteral("\n"));
    m_logEdit->moveCursor(QTextCursor::End);
}

void WeChatWorkbenchDialog::setupUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(6);

    auto* statusRow = new QHBoxLayout();
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(8);
    m_statusLabel = new QLabel(QStringLiteral("请先点击左侧「启动 Python 服务」以连接微信 UIA。"), this);
    m_statusLabel->setObjectName(QStringLiteral("wechatWorkbenchStatusLabel"));
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    statusRow->addWidget(m_statusLabel, 1);
    m_pinTopBtn = makeIconToolButton(this, QIcon(), QStringLiteral("置顶"));
    m_pinTopBtn->setObjectName(QStringLiteral("pinTopButton"));
    statusRow->addWidget(m_pinTopBtn, 0, Qt::AlignVCenter);
    root->addLayout(statusRow);

    m_envHintContainer = new QWidget(this);
    m_envHintContainer->setObjectName(QStringLiteral("wechatWorkbenchEnvHintWrap"));
    auto* envLayout = new QHBoxLayout(m_envHintContainer);
    envLayout->setContentsMargins(0, 0, 0, 0);
    envLayout->setSpacing(6);
    m_envHintLabel = new QLabel(m_envHintContainer);
    m_envHintLabel->setObjectName(QStringLiteral("wechatWorkbenchEnvHintLabel"));
    m_envHintLabel->setWordWrap(true);
    m_envHintLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    envLayout->addWidget(m_envHintLabel, 1);
    m_envHintCloseBtn = makeIconToolButton(m_envHintContainer, QIcon(), QStringLiteral("关闭提示"));
    m_envHintCloseBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    envLayout->addWidget(m_envHintCloseBtn, 0, Qt::AlignTop);
    root->addWidget(m_envHintContainer);
    m_envHintContainer->setVisible(false);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("wechatWorkbenchMainSplitter"));
    splitter->setHandleWidth(2);
    splitter->setChildrenCollapsible(false);

    auto* leftPane = new QWidget(splitter);
    leftPane->setObjectName(QStringLiteral("wechatWorkbenchLeftPanel"));
    leftPane->setMinimumWidth(180);
    leftPane->setMaximumWidth(220);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(12, 12, 12, 12);
    leftLayout->setSpacing(10);
    auto* leftHeader = new QHBoxLayout();
    leftHeader->setSpacing(0);
    auto* leftTitle = new QLabel(QStringLiteral("微信会话"), leftPane);
    leftTitle->setObjectName(QStringLiteral("wechatWorkbenchPaneTitle"));
    leftHeader->addWidget(leftTitle);
    m_refreshSessionsBtn = makeIconToolButton(
        leftPane,
        loadResourceIcon(QStringLiteral(":/refresh_platform_list_icon.svg"),
                         qApp->style()->standardIcon(QStyle::SP_BrowserReload)),
        QStringLiteral("刷新会话"));
    leftHeader->addWidget(m_refreshSessionsBtn);
    leftHeader->addStretch(1);
    m_scriptServiceBtn = makeIconToolButton(
        leftPane,
        loadResourceIcon(QStringLiteral(":/startup_script_icon.svg"),
                         qApp->style()->standardIcon(QStyle::SP_MediaPlay)),
        QStringLiteral("启动 Python 服务"));
    leftHeader->addWidget(m_scriptServiceBtn);
    applyWorkbenchSessionHeaderToolButtonSize(m_refreshSessionsBtn);
    applyWorkbenchSessionHeaderToolButtonSize(m_scriptServiceBtn);
    leftLayout->addLayout(leftHeader);
    m_sessionList = new QListWidget(leftPane);
    m_sessionList->setObjectName(QStringLiteral("wechatWorkbenchSessionList"));
    m_sessionList->setAlternatingRowColors(false);
    m_sessionList->setFrameShape(QFrame::NoFrame);
    m_sessionList->setItemDelegate(new WeChatSessionListDelegate(m_sessionList));
    leftLayout->addWidget(m_sessionList, 1);
    splitter->addWidget(leftPane);

    auto* rightPane = new QWidget(splitter);
    rightPane->setObjectName(QStringLiteral("wechatWorkbenchRightPanel"));
    rightPane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    rightLayout->setSpacing(10);

    auto* chatHeader = new QWidget(rightPane);
    chatHeader->setObjectName(QStringLiteral("wechatWorkbenchChatHeader"));
    auto* chatHeaderLayout = new QHBoxLayout(chatHeader);
    chatHeaderLayout->setContentsMargins(14, 10, 14, 10);
    chatHeaderLayout->setSpacing(12);
    auto* titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(2);
    m_chatTitleLabel = new QLabel(QStringLiteral("未选择会话"), chatHeader);
    m_chatTitleLabel->setObjectName(QStringLiteral("wechatWorkbenchChatTitle"));
    m_chatMetaLabel = new QLabel(QStringLiteral("从左侧选择一个微信会话开始查看消息。"), chatHeader);
    m_chatMetaLabel->setObjectName(QStringLiteral("wechatWorkbenchChatMeta"));
    titleCol->addWidget(m_chatTitleLabel);
    titleCol->addWidget(m_chatMetaLabel);
    chatHeaderLayout->addLayout(titleCol, 1);
    m_refreshMessagesBtn = new QPushButton(QStringLiteral("刷新消息"), chatHeader);
    chatHeaderLayout->addWidget(m_refreshMessagesBtn);
    rightLayout->addWidget(chatHeader, 0);

    m_messageList = new QListWidget(rightPane);
    m_messageList->setObjectName(QStringLiteral("wechatWorkbenchMessageList"));
    m_messageList->setSelectionMode(QAbstractItemView::NoSelection);
    m_messageList->setFocusPolicy(Qt::NoFocus);
    m_messageList->setSpacing(6);
    m_messageList->hide();

    auto* composerPanel = new QWidget(rightPane);
    composerPanel->setObjectName(QStringLiteral("wechatWorkbenchComposerPanel"));
    composerPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* composerLayout = new QVBoxLayout(composerPanel);
    composerLayout->setContentsMargins(14, 12, 14, 12);
    composerLayout->setSpacing(10);

    auto* aiRow = new QHBoxLayout();
    aiRow->setSpacing(8);
    aiRow->addWidget(new QLabel(QStringLiteral("模型"), composerPanel));
    m_modelCombo = new QComboBox(composerPanel);
    m_modelCombo->addItem(QStringLiteral("DeepSeek"), QStringLiteral("deepseek:deepseek-chat"));
    m_modelCombo->addItem(QStringLiteral("豆包"), QStringLiteral("doubao:ark"));
    aiRow->addWidget(m_modelCombo);
    aiRow->addStretch(1);
    composerLayout->addLayout(aiRow);

    m_replyEdit = new QPlainTextEdit(composerPanel);
    m_replyEdit->setObjectName(QStringLiteral("wechatWorkbenchReplyEdit"));
    m_replyEdit->setPlaceholderText(QStringLiteral("输入消息，Ctrl+Enter 发送"));
    m_replyEdit->setMinimumHeight(72);
    m_replyEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_replyEdit->installEventFilter(this);
    composerLayout->addWidget(m_replyEdit, 1);

    auto* sendRow = new QHBoxLayout();
    sendRow->addStretch(1);
    m_generateSuggestionBtn = new QPushButton(QStringLiteral("生成本条回复"), composerPanel);
    m_generateSuggestionBtn->setObjectName(QStringLiteral("wechatWorkbenchPrimaryButton"));
    sendRow->addWidget(m_generateSuggestionBtn);
    m_sendBtn = new QPushButton(QStringLiteral("发送"), composerPanel);
    m_sendBtn->setObjectName(QStringLiteral("wechatWorkbenchPrimaryButton"));
    sendRow->addWidget(m_sendBtn);
    composerLayout->addLayout(sendRow);

    auto* logTitle = new QLabel(QStringLiteral("Python 服务日志"), composerPanel);
    logTitle->setObjectName(QStringLiteral("wechatWorkbenchSectionTitle"));
    composerLayout->addWidget(logTitle);

    m_logEdit = new QPlainTextEdit(composerPanel);
    m_logEdit->setObjectName(QStringLiteral("wechatWorkbenchLogEdit"));
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumBlockCount(800);
    m_logEdit->setMinimumHeight(72);
    m_logEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    composerLayout->addWidget(m_logEdit, 2);

    rightLayout->addWidget(composerPanel, 1);

    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes(QList<int>{205, 1035});
    root->addWidget(splitter, 1);

    connect(m_pinTopBtn, &QToolButton::clicked, this, &WeChatWorkbenchDialog::onPinTopClicked);
    connect(m_envHintCloseBtn, &QToolButton::clicked, this,
            &WeChatWorkbenchDialog::onEnvHintCloseClicked);
    connect(m_refreshSessionsBtn, &QToolButton::clicked, this,
            &WeChatWorkbenchDialog::onRefreshSessionsClicked);
    connect(m_refreshMessagesBtn, &QPushButton::clicked, this,
            &WeChatWorkbenchDialog::onRefreshMessagesClicked);
    connect(m_scriptServiceBtn, &QToolButton::clicked, this,
            &WeChatWorkbenchDialog::onScriptServiceControlClicked);
    connect(m_generateSuggestionBtn, &QPushButton::clicked, this,
            &WeChatWorkbenchDialog::onGenerateSuggestionClicked);
    connect(m_sendBtn, &QPushButton::clicked, this, &WeChatWorkbenchDialog::onSendClicked);
    connect(m_sessionList, &QListWidget::itemClicked, this,
            &WeChatWorkbenchDialog::onSessionItemActivated);
}

void WeChatWorkbenchDialog::refreshAll()
{
    m_service->probeStatus();
    m_service->listSessions();
}

void WeChatWorkbenchDialog::updateStatusBanner(const QString& text, bool isError)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setProperty("errorState", isError);
    style()->unpolish(m_statusLabel);
    style()->polish(m_statusLabel);
}

void WeChatWorkbenchDialog::refreshConversationHeader(const QString& metaText)
{
    if (m_chatTitleLabel) {
        m_chatTitleLabel->setText(m_currentConversationName.isEmpty()
                                      ? QStringLiteral("未选择会话")
                                      : m_currentConversationName);
    }
    if (m_chatMetaLabel) {
        m_chatMetaLabel->setText(metaText.isEmpty()
                                     ? QStringLiteral("从左侧选择一个微信会话开始查看消息。")
                                     : metaText);
    }
}

void WeChatWorkbenchDialog::populateSessions(const QJsonObject& data)
{
    const QString current = data.value(QStringLiteral("current_session")).toString().trimmed();
    if (!current.isEmpty())
        m_currentConversationName = current;
    refreshConversationHeader(QStringLiteral("左侧为当前可见会话列表，点击后读取该会话的可见消息。"));

    m_updatingSessionList = true;
    m_sessionList->clear();
    const QJsonArray sessions = data.value(QStringLiteral("sessions")).toArray();
    for (const QJsonValue& value : sessions) {
        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
            continue;
        auto* item = new QListWidgetItem(name, m_sessionList);
        item->setData(Qt::UserRole, name);
        item->setToolTip(obj.value(QStringLiteral("rect")).toString());
        if (name == m_currentConversationName)
            m_sessionList->setCurrentItem(item);
    }
    m_updatingSessionList = false;

    if (sessions.isEmpty())
        updateStatusBanner(QStringLiteral("当前未读取到可见微信会话。"), true);
}

void WeChatWorkbenchDialog::populateMessages(const QJsonObject& data)
{
    const QJsonObject conversation = data.value(QStringLiteral("conversation")).toObject();
    const QString current = conversation.value(QStringLiteral("name")).toString().trimmed();
    if (!current.isEmpty())
        m_currentConversationName = current;

    m_currentMessages = data.value(QStringLiteral("messages")).toArray();
    if (m_messageList) {
        m_messageList->clear();
        if (m_messageList->isVisible()) {
            for (const QJsonValue& value : m_currentMessages) {
                const QJsonObject msg = value.toObject();
                auto* item = new QListWidgetItem();
                item->setFlags(Qt::ItemIsEnabled);
                auto* widget = createMessageBubbleWidget(msg, m_theme, m_messageList);
                item->setSizeHint(widget->sizeHint());
                m_messageList->addItem(item);
                m_messageList->setItemWidget(item, widget);
            }
        }
    }

    const QJsonObject summary = data.value(QStringLiteral("summary")).toObject();
    refreshConversationHeader(
        QStringLiteral("当前可见消息 %1 条，unknown %2 条。")
            .arg(summary.value(QStringLiteral("count")).toInt())
            .arg(summary.value(QStringLiteral("unknown_count")).toInt()));
    updateStatusBanner(
        QStringLiteral("当前会话：%1，可见消息 %2 条（unknown %3 条）。")
            .arg(m_currentConversationName.isEmpty() ? QStringLiteral("未命名") : m_currentConversationName)
            .arg(summary.value(QStringLiteral("count")).toInt())
            .arg(summary.value(QStringLiteral("unknown_count")).toInt()));
}

QString WeChatWorkbenchDialog::currentSessionName() const
{
    if (m_sessionList && m_sessionList->currentItem())
        return m_sessionList->currentItem()->data(Qt::UserRole).toString().trimmed();
    return m_currentConversationName;
}

QString WeChatWorkbenchDialog::currentPresetKey() const
{
    if (!m_modelCombo)
        return QStringLiteral("deepseek:deepseek-chat");
    return m_modelCombo->currentData().toString();
}

void WeChatWorkbenchDialog::setControlsEnabled(bool enabled)
{
    if (m_refreshSessionsBtn)
        m_refreshSessionsBtn->setEnabled(enabled);
    if (m_refreshMessagesBtn)
        m_refreshMessagesBtn->setEnabled(enabled);
    if (m_scriptServiceBtn)
        m_scriptServiceBtn->setEnabled(enabled);
    if (m_generateSuggestionBtn)
        m_generateSuggestionBtn->setEnabled(enabled);
    if (m_sendBtn)
        m_sendBtn->setEnabled(enabled);
}
